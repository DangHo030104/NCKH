#define MQTT_KEEPALIVE 60
#define MQTT_MAX_PACKET_SIZE 512

#include <Arduino.h>
#include <LoRa_E32.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

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

LoRa_E32 e32ttl100(16, 17, &Serial2, -1, 4, 5, UART_BPS_RATE_9600, SERIAL_8N1);

/* SENSOR DATA */

float T = 0;
float H = 0;
float SM1 = 0;
float SM2 = 0;

/* MQTT PUBLISH */

unsigned long lastMsg = 0;
bool dataUpdated = false; // Flag: có dữ liệu mới để publish

/* MASTER REQUEST (ESP32 -> STM32) */

unsigned long lastRequest = 0;
const unsigned long REQUEST_INTERVAL = 30000;
const unsigned long DATA_TIMEOUT_MS = 1000;
const unsigned long CMD_ACK_TIMEOUT_MS = 1000;
const uint8_t MAX_CMD_RETRIES = 3;

unsigned long lastMqttReconnectAttempt = 0;            
const unsigned long MQTT_RECONNECT_INTERVAL = 5000;

/* SEQUENCE NUMBER */

uint32_t sequenceNumber = 0;
uint32_t waitingSeq = 0;

/* LORA STATE MACHINE */

enum LoraState
{
    LORA_IDLE,
    WAIT_DATA,
    WAIT_CMD_ACK
};

LoraState loraState = LORA_IDLE;

/* PENDING COMMAND BUFFER */

bool commandPending = false;
String pendingCommand = "";               // Command mới nhất nhận được từ MQTT.
String pendingCommandFrame = "";
String activeCommand = "";                // Command đang được gửi đi, chờ ACK
unsigned long loraStateStartedAt = 0;     // Lưu thời điểm bắt đầu chờ DATA hoặc ACK
uint8_t commandRetryCount = 0;

/* Get value from frame */
void getValue(const String &frame, const String &key, float &value)
{
    int pos = frame.indexOf(key); // Tìm vị trí đầu tiên của key trong frame

    if (pos == -1) return;
    pos += key.length(); // Di chuyển (pointer) đến sau key để lấy value

    int end = frame.indexOf(',', pos);

    if (end == -1)
    {
        end = frame.indexOf('>', pos);
    }

    if (end == -1) return; // Frame không hợp lệ

    value = frame.substring(pos, end).toFloat();
}

/* Get sequence number from frame */
uint32_t getSeq(const String &frame)
{
    int pos = frame.indexOf("SEQ=");

    if (pos == -1) return 0;

    pos += 4;

    int end = frame.indexOf(',', pos);

    if (end == -1)
    {
        end = frame.indexOf('>', pos);
    }

    if (end == -1) return 0;

    return frame.substring(pos, end).toInt();
}

void handleDataFrame(const String &frame)
{
    getValue(frame, "T=", T);
    getValue(frame, "H=", H);
    getValue(frame, "SM1=", SM1);
    getValue(frame, "SM2=", SM2);

    Serial.println();

    Serial.printf("Temperature: %.2f °C | Humidity: %.2f %% | SoilMoisture1: %.2f %% | SoilMoisture2: %.2f %%\n", T, H, SM1, SM2);
}

/* MQTT Client tự gọi hàm này khi nhận message từ MQTT Server */
void mqttCallback(char *topic, byte *payload, unsigned int length)
{
    String messageTemp = "";

    for (unsigned int i = 0; i < length; i++)
    {
        messageTemp += (char)payload[i];
    }

    Serial.println();
    Serial.println("===================== MQTT Message Received ==================");

    Serial.print("MQTT Topic: ");
    Serial.print(topic);
    Serial.print(" | MQTT Message: ");
    Serial.println(messageTemp);

    /* PARSE JSON */
    JsonDocument doc;

    DeserializationError error = deserializeJson(doc, messageTemp); // Phân tích chuỗi JSON và lưu vào doc.

    if (error)
    {
        Serial.print("JSON Error: ");
        Serial.println(error.c_str());

        return;
    }

    /* GET RELAY + STATE */
    int relay = doc["relay"] | 0; // Đọc trường relay (1 hoặc 2)

    const char *state = doc["state"]; // Lấy trường state (ON/OFF)

    if (relay == 0 || state == nullptr)
    {
        Serial.println("Invalid MQTT command");
        return;
    }

    /* ZONE 1 */
    if (relay == 1)
    {
        if (strcmp(state, "ON") == 0) // So sánh chuỗi
        {
            pendingCommand = "ZONE=1,IRR=ON";
            commandPending = true;
        }
        else if (strcmp(state, "OFF") == 0)
        {
            pendingCommand = "ZONE=1,IRR=OFF";
            commandPending = true;
        }
    }

    /* ZONE 2 */
    else if (relay == 2)
    {
        if (strcmp(state, "ON") == 0)
        {
            pendingCommand = "ZONE=2,IRR=ON";
            commandPending = true;
        }
        else if (strcmp(state, "OFF") == 0)
        {
            pendingCommand = "ZONE=2,IRR=OFF";
            commandPending = true;
        }
    }

    if (commandPending)
    {
        Serial.print("CMD queued: ");
        Serial.println(pendingCommand);
    }
}

void reconnectMQTT()
{
    if (mqttClient.connected()) return;

    if (lastMqttReconnectAttempt != 0 && millis() - lastMqttReconnectAttempt < MQTT_RECONNECT_INTERVAL)
    {
        return;
    }

    lastMqttReconnectAttempt = millis();

    {
        Serial.print("Connecting HiveMQ Cloud...");

        // Random Client ID tránh 2 client dùng cùng ID, ví dụ ESP32_Client-a83f
        String clientId = "ESP32_Client-" + String(random(0, 0xffff), HEX);

        if (mqttClient.connect(clientId.c_str(), mqtt_user, mqtt_password))
        {
            Serial.println("\nConnected!");
            lastMqttReconnectAttempt = 0;

            // Subscribe topic: control -> để broker chuyển message từ web về ESP32
            mqttClient.subscribe(subscribe_topic);
 
            Serial.print("Subscribed: ");
            Serial.println(subscribe_topic);
        }
        else
        {
            Serial.print("\nFailed, rc=");
            Serial.println(mqttClient.state());

        }
    }
}

void publishData()
{
    JsonDocument doc;   // Tạo JSON document.

    doc["T"] = T;
    doc["H"] = H;
    doc["SM1"] = SM1;
    doc["SM2"] = SM2;

    char payload[200];

    serializeJson(doc, payload, sizeof(payload));   // Chuyển JSON thành chuỗi và ghi vào payload

    bool result = mqttClient.publish(publish_topic, payload);

    Serial.println();

    Serial.print("MQTT Publish: ");
    Serial.print(payload);

    Serial.print(" | Topic: ");
    Serial.println(publish_topic);

    Serial.print("Publish status: ");
    if (result)
    {
        Serial.println("SUCCESS");
    }
    else
    {
        Serial.println("FAILED");
    }
}

void sendRequest()
{
    sequenceNumber++;

    if (sequenceNumber == 0)
    {
        sequenceNumber = 1;
    }

    waitingSeq = sequenceNumber;    // Ghi nhớ seq mà DATA phải chứa.

    String req = "<REQ,SEQ=" + String(sequenceNumber) + ">";

    ResponseStatus rs = e32ttl100.sendMessage(req);

    Serial.println();

    Serial.print("LoRa TX REQ: ");
    Serial.print(req);

    Serial.print(" | TX Status: ");
    Serial.println(rs.getResponseDescription());

    /* Sau khi TX REQ, MASTER chỉ được chờ DATA */
    loraState = WAIT_DATA;
    loraStateStartedAt = millis();
}

void transmitPendingCommandFrame()
{
    ResponseStatus rs = e32ttl100.sendMessage(pendingCommandFrame);

    Serial.println();
    Serial.print("LoRa TX CMD: ");
    Serial.print(pendingCommandFrame);
    Serial.print(" | Attempt: ");
    Serial.print(commandRetryCount + 1);
    Serial.print("/");
    Serial.print(MAX_CMD_RETRIES);
    Serial.print(" | TX Status: ");
    Serial.println(rs.getResponseDescription());

    Serial.print("Waiting ACK SEQ: ");
    Serial.println(waitingSeq);

    loraState = WAIT_CMD_ACK;
    loraStateStartedAt = millis();
}

void sendPendingCommand()
{
    if (!commandPending) return;

    sequenceNumber++;

    if (sequenceNumber == 0)
    {
        sequenceNumber = 1;
    }

    waitingSeq = sequenceNumber;

    /* Ví dụ: pendingCommand: ZONE=1,IRR=ON => <CMD,SEQ=25,ZONE=1,IRR=ON> */
    activeCommand = pendingCommand;
    pendingCommandFrame = "<CMD,SEQ=" + String(sequenceNumber) + "," + activeCommand + ">";
    commandRetryCount = 0;
    transmitPendingCommandFrame();
}

void handleAckFrame(const String &frame)
{
    uint32_t receivedSeq = getSeq(frame);

    if (loraState == WAIT_CMD_ACK && receivedSeq == waitingSeq)
    {
        Serial.println("ACK MATCH -> COMMAND SUCCESS");

        if (pendingCommand == activeCommand)
        {
            commandPending = false;
            pendingCommand = "";
        }
        pendingCommandFrame = "";
        activeCommand = "";
        commandRetryCount = 0;
        loraState = LORA_IDLE;
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

        handleDataFrame(frame);

        /* Có data mới -> cho phép publish */
        dataUpdated = true;

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
        Serial.println("DATA TIMEOUT -> RETURN TO IDLE");
        loraState = LORA_IDLE;
        return;
    }

    if (loraState == WAIT_CMD_ACK && millis() - loraStateStartedAt >= CMD_ACK_TIMEOUT_MS)
    {
        commandRetryCount++;

        if (commandRetryCount < MAX_CMD_RETRIES)
        {
            Serial.println("ACK TIMEOUT -> RETRY COMMAND");
            transmitPendingCommandFrame();
        }
        else
        {
            Serial.println("COMMAND FAILED -> MAX RETRIES REACHED");
            if (pendingCommand == activeCommand)
            {
                commandPending = false;
                pendingCommand = "";
            }
            pendingCommandFrame = "";
            activeCommand = "";
            commandRetryCount = 0;
            loraState = LORA_IDLE;
        }
    }
}

void setup()
{
    Serial.begin(115200);

    e32ttl100.begin();
    Serial.println("LoRa Receiver Started");

    WiFi.begin(ssid, password);
    Serial.print("Connecting to Wi-Fi");
    while (WiFi.status() != WL_CONNECTED)
    {
        Serial.print(".");
        delay(300);
    }
    Serial.println();
    Serial.println("Wi-Fi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    secureClient.setInsecure();                     // Bỏ kiểm tra CA, TLS vẫn mã hóa
    mqttClient.setServer(mqtt_server, mqtt_port);   // Cấu hình địa chỉ và cổng broker.
    mqttClient.setCallback(mqttCallback);           // Đăng ký hàm xử lý khi nhận message từ MQTT.

    randomSeed(millis());

    Serial.println("ESP32 MQTT Broker Ready!");
}

void loop()
{
    if (!mqttClient.connected())
    {
        reconnectMQTT();
    }
    mqttClient.loop();  // Duy trì kết nối MQTT.

    // LORA RECEIVE
    if (e32ttl100.available() > 0)
    {
        ResponseContainer rc = e32ttl100.receiveMessage();

        if (rc.data.length() > 0)
        {
            Serial.println();
            Serial.print("LoRa RX: ");
            Serial.println(rc.data);

            processLoRaFrame(rc.data);
        }
    }

    handleLoraTimeouts();

    // LORA MASTER SCHEDULER
    if (loraState == LORA_IDLE)     
    {
        /* CMD ưu tiên hơn việc polling sensor */
        if (commandPending)
        {
            sendPendingCommand();
        }

        /* Không có CMD -> Poll sensor mỗi 30 giây */
        else if (millis() - lastRequest >= REQUEST_INTERVAL)
        {
            lastRequest = millis();
            sendRequest();
        }
    }

    // MQTT PUBLISH
    if (dataUpdated && millis() - lastMsg >= 2000)
    {
        lastMsg = millis();
        publishData();
        dataUpdated = false;
    }
}
