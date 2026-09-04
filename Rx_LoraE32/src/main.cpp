#define MQTT_KEEPALIVE 60
#define MQTT_MAX_PACKET_SIZE 512

/* Private includes ----------------------------------------------------------*/
#include <Arduino.h>
#include <LoRa_E32.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <math.h>

/* Private typedef -----------------------------------------------------------*/
/* LORA STATE MACHINE */
typedef enum
{
    LORA_IDLE,
    WAIT_DATA,
    WAIT_CMD_ACK
} LoRaState;

LoRaState loraState = LORA_IDLE;

typedef struct
{
    uint8_t zone;
    bool irr;
} LoRaCommand;

typedef struct
{
    float temperature;
    float humidity;
    float soil1;
    float soil2;
} SensorData;

/* Private define/macro and Private variables ------------------------------------------------------------*/
/* LORA E32 */
#define TX_PIN 16
#define RX_PIN 17
#define AUX_PIN 18
#define M0_PIN 4
#define M1_PIN 5

/* WiFi */
const char *ssid = "Pho Tro Tret";
const char *password = "Ngoc4795";

/* HIVEMQ CLOUD */
const char *mqtt_server = "10cf23427b77452faec8dc86e09f1bc1.s1.eu.hivemq.cloud";
const int mqtt_port = 8883;
const char *mqtt_user = "tienduc";
const char *mqtt_password = "D@ucffgh123";

/* MQTT TOPIC */
// ESP32 publish data sensor lên Web
const char *publish_topic = "iot/sensor/data/301b5eb855b7485bb15e";

// ESP32 nhận command từ Web
const char *subscribe_topic = "iot/device/control/301b5eb855b7485bb15e";

/* MQTT CLIENT */
WiFiClientSecure secureClient;         // Tạo kết nối TCP có mã hóa TLS
PubSubClient mqttClient(secureClient); // Tạo MQTTClient sử dụng kết nối TCP đã mã hóa TLS.

/* LORA E32 */
LoRa_E32 e32ttl100(TX_PIN, RX_PIN, &Serial2, AUX_PIN, M0_PIN, M1_PIN, UART_BPS_RATE_9600, SERIAL_8N1);

/* SENSOR DATA */
float T = 0, H = 0, SM1 = 0, SM2 = 0;

/* MQTT PUBLISH */
unsigned long lastMsg = 0;

/* MASTER REQUEST (ESP32 -> STM32) */
unsigned long lastRequest = 0;
const unsigned long REQUEST_INTERVAL = 5000;    // 5 phút = 300000 ms

const unsigned long DATA_TIMEOUT_MS = 5000;     // Thời gian chờ DATA từ STM32 sau khi gửi REQ
const unsigned long CMD_ACK_TIMEOUT_MS = 5000;  // Thời gian chờ ACK từ STM32 sau khi gửi CMD

const uint8_t MAX_CMD_RETRIES = 3;

unsigned long lastMqttReconnectAttempt = 0;
const unsigned long MQTT_RECONNECT_INTERVAL = 5000;

unsigned long lastWiFiReconnectAttempt = 0;
const unsigned long WIFI_RECONNECT_INTERVAL = 5000;
bool wifiConnected = false;

const unsigned long MQTT_PUBLISH_INTERVAL = 2000;

/* SEQUENCE NUMBER */
uint32_t sequenceNumber = 0;
uint32_t waitingSeq = 0;

/* PENDING COMMAND BUFFER */
String pendingCommandFrame = "";      // Command frame đang được gửi đi, chờ ACK
unsigned long loraStateStartedAt = 0; // Lưu thời điểm bắt đầu chờ DATA hoặc ACK
uint8_t commandRetryCount = 0;

QueueHandle_t commandQueue;
QueueHandle_t sensorDataQueue;

void printAuxState()
{
    Serial.print(" | E32 AUX: ");

    if (digitalRead(AUX_PIN) == HIGH)
    {
        Serial.println("HIGH -> READY");
    }
    else
    {
        Serial.println("LOW -> BUSY");
    }
}

/* Get value from frame */
bool getValue(const String &frame, const String &key, float &value)
{
    int pos = frame.indexOf(key); // Tìm vị trí đầu tiên của key trong frame
    if (pos == -1)
        return false;

    pos += key.length(); // Di chuyển (pointer) đến sau key để lấy value

    int end = frame.indexOf(',', pos);
    if (end == -1)
    {
        end = frame.indexOf('>', pos);
    }

    if (end == -1)
        return false; // Frame không hợp lệ

    String valueText = frame.substring(pos, end);

    if (valueText.length() == 0)
        return false;

    char *parseEnd = nullptr;
    const float parsedValue = strtof(valueText.c_str(), &parseEnd);

    /* Kiểm tra xem chuỗi có được phân tích thành công không */
    if (parseEnd == valueText.c_str() || *parseEnd != '\0' || !isfinite(parsedValue))
        return false;

    value = parsedValue;
    return true;
}

/* Get sequence number from frame */
uint32_t getSeq(const String &frame)
{
    int pos = frame.indexOf("SEQ=");

    if (pos == -1)
        return 0;

    pos += 4;

    int end = frame.indexOf(',', pos);

    if (end == -1)
    {
        end = frame.indexOf('>', pos);
    }

    if (end == -1)
        return 0;

    return frame.substring(pos, end).toInt();
}

bool handleDataFrame(const String &frame)
{
    /* Kiểm tra frame bắt đầu bằng <DATA, và kết thúc bằng > */
    if (!frame.startsWith("<DATA,") || !frame.endsWith(">"))
        return false;

    float newT = 0;
    float newH = 0;
    float newSM1 = 0;
    float newSM2 = 0;

    /* Parse vào biến tạm, chỉ cập nhật T/H/SM1/SM2 khi đủ cả 4 trường */
    if (!getValue(frame, "T=", newT) || !getValue(frame, "H=", newH) ||
        !getValue(frame, "SM1=", newSM1) || !getValue(frame, "SM2=", newSM2))
    {
        return false;
    }

    T = newT;
    H = newH;
    SM1 = newSM1;
    SM2 = newSM2;

    Serial.printf("\nTemperature: %.2f °C | Humidity: %.2f %% | SoilMoisture1: %.2f %% | SoilMoisture2: %.2f %%\n", T, H, SM1, SM2);
    return true;
}

/* MQTT Client tự gọi hàm này khi nhận message từ MQTT Server */
void mqttCallback(char *topic, byte *payload, unsigned int length)
{
    String msg = "";

    for (unsigned int i = 0; i < length; i++)
    {
        msg += (char)payload[i];
    }

    Serial.println("\n===================== MQTT Message Received ==================");
    Serial.print("MQTT Topic: ");
    Serial.print(topic);
    Serial.print(" | MQTT Message: ");
    Serial.println(msg);

    /* PARSE JSON */
    JsonDocument doc;

    DeserializationError error = deserializeJson(doc, msg); // Phân tích chuỗi JSON và lưu vào doc.

    if (error)
    {
        Serial.print("JSON Error: ");
        Serial.println(error.c_str());
        return;
    }

    /* GET RELAY + STATE */
    int relay = doc["relay"] | 0;     // Lấy trường relay (1 hoặc 2)
    const char *state = doc["state"]; // Lấy trường state (ON/OFF)

    if (relay == 0 || state == nullptr)
    {
        Serial.println("Invalid MQTT command");
        return;
    }

    /* CREATE LORA COMMAND */
    LoRaCommand cmd = {};
    cmd.zone = relay;

    if (strcmp(state, "ON") == 0)
    {
        cmd.irr = true;
    }
    else if (strcmp(state, "OFF") == 0)
    {
        cmd.irr = false;
    }
    else
    {
        Serial.print("[ERROR] Invalid state: ");
        Serial.println(state);
        return;
    }

    /* Gửi command sang LoRaTask */
    if (xQueueSend(commandQueue, &cmd, 0) == pdPASS)
    {
        Serial.print("[MQTT] CMD queued: ZONE=");
        Serial.print(cmd.zone);
        Serial.print(" IRR=");
        Serial.println(cmd.irr ? "ON" : "OFF");
    }
    else
    {
        Serial.println("[ERROR] CommandQueue FULL");
    }
}

void maintainWiFi()
{
    wl_status_t status = WiFi.status();

    /* WIFI CONNECTED */
    if (status == WL_CONNECTED)
    {
        if (!wifiConnected)
        {
            wifiConnected = true;

            Serial.println();
            Serial.println("[WIFI] Connected");

            Serial.print("[WIFI] IP: ");
            Serial.println(WiFi.localIP());

            /* Cho phép MQTT reconnect ngay sau khi WiFi vừa trở lại */
            lastMqttReconnectAttempt = 0;
        }
        return;
    }

    /* WIFI LOST */
    if (wifiConnected)
    {
        wifiConnected = false;

        Serial.println();
        Serial.println("[WIFI] Connection lost");

        /* Khi WiFi mất thì đóng MQTT session cũ */
        if (mqttClient.connected())
        {
            mqttClient.disconnect();
            Serial.println("[MQTT] Disconnected due to WiFi loss");
        }
    }

    /* NON-BLOCKING WIFI RECONNECT */
    if (millis() - lastWiFiReconnectAttempt >= WIFI_RECONNECT_INTERVAL)
    {
        lastWiFiReconnectAttempt = millis();
        Serial.println("[WIFI] Reconnecting...");
        WiFi.reconnect();
    }
}

void reconnectMQTT()
{
    if (WiFi.status() != WL_CONNECTED)
        return;

    if (mqttClient.connected())
        return;

    if (lastMqttReconnectAttempt != 0 &&
        millis() - lastMqttReconnectAttempt < MQTT_RECONNECT_INTERVAL)
        return;

    lastMqttReconnectAttempt = millis();

    Serial.println("\n[MQTT] Connecting to HiveMQ Cloud...");

    // Random Client ID tránh 2 client dùng cùng ID, ví dụ ESP32_Client-a83f
    String clientId = "ESP32_Client-" + String(random(0, 0xffff), HEX);

    if (mqttClient.connect(clientId.c_str(), mqtt_user, mqtt_password))
    {
        Serial.println("[MQTT] Connected!");
        lastMqttReconnectAttempt = 0;

        // Subscribe control topic -> để broker chuyển message từ web về ESP32
        mqttClient.subscribe(subscribe_topic);

        Serial.print("[MQTT] Subscribed: ");
        Serial.println(subscribe_topic);
    }
    else
    {
        Serial.print("[MQTT] Connect failed, rc=");
        Serial.println(mqttClient.state());
    }
}

void publishData(const SensorData &data)
{
    JsonDocument doc; // Tạo JSON document.

    doc["T"] = data.temperature;
    doc["H"] = data.humidity;
    doc["SM1"] = data.soil1;
    doc["SM2"] = data.soil2;

    char payload[200];

    serializeJson(doc, payload, sizeof(payload)); // Chuyển JSON thành chuỗi và ghi vào payload

    bool result = mqttClient.publish(publish_topic, payload);

    Serial.print("\nMQTT Publish: ");
    Serial.print(payload);
    Serial.print(" | Topic: ");
    Serial.println(publish_topic);
    Serial.print("Publish status: ");
    Serial.println(result ? "SUCCESS" : "FAILED");
}

void sendRequest()
{
    sequenceNumber++;

    if (sequenceNumber == 0)
    {
        sequenceNumber = 1;
    }

    waitingSeq = sequenceNumber; // Ghi nhớ seq mà DATA phải chứa.

    String req = "<REQ,SEQ=" + String(sequenceNumber) + ">";

    /* 1. WAKE-UP MODE */
    Status s1 = e32ttl100.setMode(MODE_1_WAKE_UP);

    Serial.print("\nE32 Wake-up mode: ");
    Serial.print(getResponseDescriptionByParams(s1));
    printAuxState();

    /* 2. SEND WAKE-UP REQ  */
    ResponseStatus rs = e32ttl100.sendMessage(req);

    Serial.print("LoRa TX REQ: ");
    Serial.print(req);
    Serial.print(" | TX Status: ");
    Serial.println(rs.getResponseDescription());

    /* 3. RETURN MASTER TO NORMAL MODE */
    Status s2 = e32ttl100.setMode(MODE_0_NORMAL);

    Serial.print("E32 Master -> NORMAL: ");
    Serial.print(getResponseDescriptionByParams(s2));
    printAuxState();

    /* Cho E32 ổn định hoàn toàn ở RX NORMAL */
    delay(20);

    /* Sau khi TX REQ, MASTER chỉ được chờ DATA */
    loraState = WAIT_DATA;
    loraStateStartedAt = millis();
}

void transmitPendingCommandFrame()
{
    /* Wake-up transmitter */
    Status s1 = e32ttl100.setMode(MODE_1_WAKE_UP);

    Serial.print("\nE32 Wake-up mode: ");
    Serial.print(getResponseDescriptionByParams(s1));
    printAuxState();

    /* Send command frame */
    ResponseStatus rs = e32ttl100.sendMessage(pendingCommandFrame);

    Serial.print("LoRa TX CMD: ");
    Serial.print(pendingCommandFrame);
    Serial.print(" | Attempt: ");
    Serial.print(commandRetryCount + 1);
    Serial.print("/");
    Serial.print(MAX_CMD_RETRIES);
    Serial.print(" | TX Status: ");
    Serial.println(rs.getResponseDescription());

    /* CMD đã gửi xong -> quay về NORMAL để nhận ACK */
    Status s2 = e32ttl100.setMode(MODE_0_NORMAL);

    Serial.print("E32 Master -> NORMAL: ");
    Serial.print(getResponseDescriptionByParams(s2));
    printAuxState();

    /* Cho E32 ổn định hoàn toàn ở RX NORMAL */
    delay(20);

    Serial.print("Waiting ACK SEQ: ");
    Serial.println(waitingSeq);

    loraState = WAIT_CMD_ACK;
    loraStateStartedAt = millis();
}

void sendPendingCommand(const LoRaCommand &cmd)
{
    sequenceNumber++;

    if (sequenceNumber == 0)
    {
        sequenceNumber = 1;
    }

    waitingSeq = sequenceNumber;

    /* Ví dụ: pendingCommand: ZONE=1,IRR=ON => <CMD,SEQ=25,ZONE=1,IRR=ON> */
    pendingCommandFrame = "<CMD,SEQ=" + String(sequenceNumber) + "," + "ZONE=" + String(cmd.zone) + ",IRR=" + String(cmd.irr ? "ON" : "OFF") + ">";

    commandRetryCount = 0;
    transmitPendingCommandFrame();
}

void handleAckFrame(const String &frame)
{
    uint32_t receivedSeq = getSeq(frame);

    if (loraState == WAIT_CMD_ACK && receivedSeq == waitingSeq)
    {
        Serial.println("ACK MATCH -> COMMAND SUCCESS");

        pendingCommandFrame = "";
        commandRetryCount = 0;
        loraState = LORA_IDLE;

        /* Reset timer REQ -> Tránh gửi REQ ngay sau khi nhận ACK */
        lastRequest = millis(); 
    }
    else
    {
        Serial.println("ACK INVALID -> IGNORED");
    }
}

void handleReceivedData(const String &frame)
{
    uint32_t receivedSeq = getSeq(frame);

    if (loraState == WAIT_DATA && receivedSeq == waitingSeq)
    {
        Serial.println("DATA SEQ MATCH");

        if (!handleDataFrame(frame))
        {
            Serial.println("INVALID DATA PAYLOAD -> IGNORED");
            return;
        }

        SensorData data;
        data.temperature = T;
        data.humidity = H;
        data.soil1 = SM1;
        data.soil2 = SM2;

        if (xQueueSend(sensorDataQueue, &data, 0) == pdPASS)
        {
            Serial.println("[QUEUE] Sensor data queued");
        }
        else
        {
            Serial.println("[ERROR] SensorDataQueue FULL");
        }

        loraState = LORA_IDLE;
    }
    else
    {
        Serial.println("INVALID DATA -> IGNORED");
    }
}

/* Phân loại frame LoRa nhận được */
void processLoRaFrame(const String &frame)
{
    // DATA
    if (frame.startsWith("<DATA"))
    {
        handleReceivedData(frame);
        return;
    }

    // ACK
    if (frame.startsWith("<ACK"))
    {
        handleAckFrame(frame);
        return;
    }

    Serial.print("Unknown LoRa Frame: ");
    Serial.println(frame);
}

void handleLoraTimeouts()
{
    if (loraState == WAIT_DATA && millis() - loraStateStartedAt >= DATA_TIMEOUT_MS) /**/
    {
        Serial.println("\nDATA TIMEOUT -> RETURN TO IDLE");
        loraState = LORA_IDLE;
        return;
    }

    if (loraState == WAIT_CMD_ACK && millis() - loraStateStartedAt >= CMD_ACK_TIMEOUT_MS)
    {
        commandRetryCount++;

        if (commandRetryCount < MAX_CMD_RETRIES)
        {
            Serial.println("\nACK TIMEOUT -> RETRY COMMAND");
            transmitPendingCommandFrame();
        }
        else
        {
            Serial.println("\nCOMMAND FAILED -> MAX RETRIES REACHED");

            pendingCommandFrame = "";
            commandRetryCount = 0;

            loraState = LORA_IDLE;
        }
    }
}

void LoRaTask(void *pvParameters)
{
    Serial.println("[RTOS] LoRaTask started!");

    LoRaCommand cmd;

    for (;;)
    {
        /* LORA RECEIVE  */
        if (e32ttl100.available() > 0)
        {
            ResponseContainer rc = e32ttl100.receiveMessage();

            if (rc.data.length() > 0)
            {
                Serial.print("\nLoRa RX: ");
                Serial.println(rc.data);

                processLoRaFrame(rc.data);
            }
        }

        /* TIMEOUT  */
        handleLoraTimeouts();

        /* MASTER SCHEDULER */
        if (loraState == LORA_IDLE)
        {
            /* CMD ưu tiên hơn polling */
            if (xQueueReceive(commandQueue, &cmd, 0) == pdPASS)
            {
                sendPendingCommand(cmd);
            }

            /* Không có CMD -> Polling sensor */
            else if (millis() - lastRequest >= REQUEST_INTERVAL)
            {
                lastRequest = millis();
                sendRequest();
            }
        }

        /* Nhường CPU cho task khác 1ms */
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void MQTTTask(void *pvParameters)
{
    Serial.println("[RTOS] MQTTTask started");

    SensorData data;

    for (;;)
    {
        maintainWiFi();

        /* Chỉ kết nối và duy trì MQTT khi WiFi Ready */
        if (WiFi.status() == WL_CONNECTED)
        {
            if (!mqttClient.connected())
            {
                reconnectMQTT();
            }

            if (mqttClient.connected())
            {
                /* Duy trì MQTT connection */
                mqttClient.loop();

                /* Có sensor data mới? */
                if (xQueueReceive(sensorDataQueue, &data, 0) == pdPASS)
                {
                    /* MQTT PUBLISH */
                    publishData(data);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

void setup()
{
    Serial.begin(115200);

    /* LoRa E32 */
    e32ttl100.begin();
    Serial.println("LoRa Receiver Started");

    /* WiFi */
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    lastWiFiReconnectAttempt = millis();
    wifiConnected = false;
    Serial.println("[WIFI] Connecting in background...");

    /* MQTT */
    secureClient.setInsecure();                   // Bỏ kiểm tra CA, TLS vẫn mã hóa
    mqttClient.setServer(mqtt_server, mqtt_port); // Cấu hình địa chỉ và cổng broker.
    mqttClient.setCallback(mqttCallback);         // Đăng ký hàm xử lý khi nhận message từ MQTT.

    randomSeed(millis());

    /* Create Queues */
    commandQueue = xQueueCreate(5, sizeof(LoRaCommand));
    sensorDataQueue = xQueueCreate(5, sizeof(SensorData));

    if (commandQueue == NULL || sensorDataQueue == NULL)
    {
        Serial.println("[ERROR] Queue creation failed");

        while (1)
        {
            delay(1000);
        }
    }

    Serial.println("[RTOS] Queues created!");

    /* Create RTOS Tasks */
    xTaskCreate(
        LoRaTask,   // Task function
        "LoRaTask", // Task name
        4096,       // Stack size (bytes)
        NULL,       // Task parameters
        2,          // Task priority
        NULL);      // Task handle

    xTaskCreate(
        MQTTTask,
        "MQTTTask",
        4096,
        NULL,
        1,
        NULL);

    Serial.println("[RTOS] Tasks created!");
}

void loop()
{
    /* Arduino loopTask Block và nhường CPU 1s */
    vTaskDelay(pdMS_TO_TICKS(1000));
}
