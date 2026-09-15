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
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <time.h>

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

    uint32_t seq;
    uint32_t receivedAt;

    bool loraConnected;
    bool mqttConnected;
    bool wifiConnected;

} SensorData;

/* Private define/macro and Private variables ------------------------------------------------------------*/
/* LORA E32 */
#define TX_PIN 16
#define RX_PIN 17
#define AUX_PIN 27
#define M0_PIN 25
#define M1_PIN 26

/* TFT DISPLAY */
#define TFT_CS 5
#define TFT_RST 21
#define TFT_DC 19
#define TFT_SCLK 18
#define TFT_MOSI 23

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

/* TFT DISPLAY */
SPIClass spi = SPIClass(VSPI);
Adafruit_ST7735 tft = Adafruit_ST7735(&spi, TFT_CS, TFT_DC, TFT_RST);

/* SENSOR DATA */
float T = 0, H = 0, SM1 = 0, SM2 = 0;

/* MQTT PUBLISH */
unsigned long lastMsg = 0;

/* MASTER REQUEST (ESP32 -> STM32) */
unsigned long lastRequest = 0;
const unsigned long REQUEST_INTERVAL = 10000; // 5 phút = 300000 ms

const unsigned long DATA_TIMEOUT_MS = 5000;    // Time Wait DATA STM32 sau khi gửi REQ
const unsigned long CMD_ACK_TIMEOUT_MS = 5000; // Time Wait ACK STM32 sau khi gửi CMD

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

/* RTOS TASKS */
TaskHandle_t loraTaskHandle;
TaskHandle_t mqttTaskHandle;
TaskHandle_t displayTaskHandle;

/* RTOS QUEUES */
QueueHandle_t commandQueue;
QueueHandle_t mqttDataQueue;
QueueHandle_t displayQueue;

enum class CommandDisplayState : uint8_t { NONE, WAIT, ACK, FAIL };
struct DashboardStatus
{
    bool mqttOnline = false;
    uint8_t zone = 0;
    bool irrigationOn = false;
    uint8_t attempt = 0;
    CommandDisplayState command = CommandDisplayState::NONE;
};
DashboardStatus dashboardStatus;
portMUX_TYPE dashboardMux = portMUX_INITIALIZER_UNLOCKED;

// Copy shared status under a short lock; never draw or use MQTT under the lock.
DashboardStatus readDashboardStatus()
{
    portENTER_CRITICAL(&dashboardMux);
    DashboardStatus snapshot = dashboardStatus;
    portEXIT_CRITICAL(&dashboardMux);
    return snapshot;
}

void setCommandDisplayState(CommandDisplayState state)
{
    portENTER_CRITICAL(&dashboardMux);
    dashboardStatus.command = state;
    dashboardStatus.attempt = commandRetryCount + 1;
    portEXIT_CRITICAL(&dashboardMux);
}

void setMqttDisplayState(bool online)
{
    portENTER_CRITICAL(&dashboardMux);
    dashboardStatus.mqttOnline = online;
    portEXIT_CRITICAL(&dashboardMux);
}

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
            // Start/restart background NTP synchronization on Wi-Fi connection.
            // Vietnam uses UTC+7 with no daylight saving time.
            configTime(7 * 3600, 0, "pool.ntp.org", "time.google.com");

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

    setMqttDisplayState(false);

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
    setCommandDisplayState(CommandDisplayState::WAIT);
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
    portENTER_CRITICAL(&dashboardMux);
    dashboardStatus.zone = cmd.zone;
    dashboardStatus.irrigationOn = cmd.irr;
    portEXIT_CRITICAL(&dashboardMux);
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
        setCommandDisplayState(CommandDisplayState::ACK);

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

        SensorData data = {};
        data.temperature = T;
        data.humidity = H;
        data.soil1 = SM1;
        data.soil2 = SM2;
        data.seq = receivedSeq;
        data.receivedAt = millis();

        /* Send data to MQTT queue */
        if (xQueueSend(mqttDataQueue, &data, 0) == pdPASS)
        {
            Serial.println("[QUEUE] MQTT Queue OK");
        }
        else
        {
            Serial.println("[ERROR] MQTT Queue FULL");
        }

        /* Send data to Display queue */
        if (xQueueSend(displayQueue, &data, 0) == pdPASS)
        {
            Serial.println("[QUEUE] Display Queue OK");
        }
        else
        {
            Serial.println("[ERROR] Display Queue FULL");
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

            setCommandDisplayState(CommandDisplayState::FAIL);
            pendingCommandFrame = "";
            commandRetryCount = 0;

            loraState = LORA_IDLE;
        }
    }
}

// Render off-screen, then transfer only changed dashboard regions.
// One RGB565 frame uses 40 KiB; no clear operation is sent to the TFT.
GFXcanvas16 dashboardFrame(160, 128);

void flushDashboardRegion(int x, int y, int width, int height)
{
    uint16_t *pixels = dashboardFrame.getBuffer();
    if (x == 0 && width == 160)
        tft.drawRGBBitmap(x, y, pixels + y * 160, width, height);
    else
        for (int row = y; row < y + height; ++row)
            tft.drawRGBBitmap(x, row, pixels + row * 160 + x, width, 1);
}

// Compact dashboard for the landscape 160 x 128 TFT.
void drawSensorCard(int x, int y, const char *label, float value, bool temperature, uint16_t accent)
{
    const uint16_t card = 0x10E4;
    dashboardFrame.fillRoundRect(x, y, 74, 38, 4, card);
    if (temperature)
    {
        dashboardFrame.drawRoundRect(x + 7, y + 5, 5, 10, 2, accent);
        dashboardFrame.fillCircle(x + 9, y + 15, 3, accent);
        dashboardFrame.drawFastVLine(x + 9, y + 8, 7, accent);
    }
    else
    {
        dashboardFrame.fillTriangle(x + 9, y + 5, x + 5, y + 12, x + 13, y + 12, accent);
        dashboardFrame.fillCircle(x + 9, y + 13, 4, accent);
        dashboardFrame.drawPixel(x + 7, y + 13, card);
    }
    dashboardFrame.setTextSize(1);
    dashboardFrame.setTextColor(0x8C92);
    dashboardFrame.setCursor(x + 20, y + 7);
    dashboardFrame.print(label);
    char number[20];
    if (isfinite(value))
        snprintf(number, sizeof(number), "%.1f", value);
    else
        snprintf(number, sizeof(number), "--");
    dashboardFrame.setTextSize(strlen(number) <= 5 ? 2 : 1);
    dashboardFrame.setTextColor(ST77XX_WHITE);
    dashboardFrame.setCursor(x + 5, y + 21);
    dashboardFrame.print(number);
    dashboardFrame.setTextSize(1);
    dashboardFrame.setTextColor(accent);
    // Draw the degree mark directly so it does not depend on UTF-8 font support.
    if (temperature)
        dashboardFrame.drawCircle(x + 62, y + 26, 1, accent);
    dashboardFrame.setCursor(x + 65, y + 27);
    dashboardFrame.print(temperature ? "C" : "%");
}

void updateDisplay(const SensorData &data, bool hasData, uint32_t receivedAt)
{
    const uint16_t bg = 0x0862, green = 0x4ED2, muted = 0x8C92;
    if (!dashboardFrame.getBuffer())
        return;
    dashboardFrame.setTextWrap(false);
    const DashboardStatus status = readDashboardStatus();
    const bool wifiOnline = WiFi.status() == WL_CONNECTED;
    const uint32_t age = millis() - receivedAt;
    const bool stale = hasData && age >= 30000;
    // Update the header and footer independently from the sensor cards.
    dashboardFrame.fillRect(0, 0, 160, 21, bg);
    dashboardFrame.setTextSize(1);
    dashboardFrame.setTextColor(green);
    dashboardFrame.setCursor(5, 7);
    char clockText[6] = "--:--";
    const time_t now = time(nullptr);
    struct tm localTime = {};
    // Read the system clock without waiting for an NTP response.
    // Before initial synchronization ESP32 time is near the Unix epoch.
    if (now >= 1704067200 && localtime_r(&now, &localTime) != nullptr)
        strftime(clockText, sizeof(clockText), "%H:%M", &localTime);
    dashboardFrame.print(clockText);
    const uint16_t wifiColor = wifiOnline ? green : ST77XX_RED;
    // Compact 13 x 10 Wi-Fi fan, centered vertically beside the label.
    // Fixed shape indicates connectivity, not RSSI.
    dashboardFrame.drawFastHLine(51, 6, 7, wifiColor);
    dashboardFrame.drawLine(48, 8, 50, 7, wifiColor);
    dashboardFrame.drawLine(58, 7, 60, 8, wifiColor);
    dashboardFrame.drawFastHLine(52, 9, 5, wifiColor);
    dashboardFrame.drawPixel(51, 10, wifiColor);
    dashboardFrame.drawPixel(57, 10, wifiColor);
    dashboardFrame.drawFastHLine(53, 12, 3, wifiColor);
    dashboardFrame.drawPixel(54, 15, wifiColor);
    dashboardFrame.setTextColor(wifiColor);
    dashboardFrame.setCursor(65, 7);
    dashboardFrame.print("WiFi");
    const uint16_t radioColor = !hasData ? 0xFD68 : (stale ? ST77XX_RED : green);
    // Compact 13 x 13 antenna with paired waves and a stable base.
    dashboardFrame.drawLine(115, 4, 113, 6, radioColor);
    dashboardFrame.drawFastVLine(113, 7, 2, radioColor);
    dashboardFrame.drawLine(113, 9, 115, 11, radioColor);
    dashboardFrame.drawLine(123, 4, 125, 6, radioColor);
    dashboardFrame.drawFastVLine(125, 7, 2, radioColor);
    dashboardFrame.drawLine(125, 9, 123, 11, radioColor);
    dashboardFrame.drawPixel(117, 6, radioColor);
    dashboardFrame.drawFastVLine(116, 7, 2, radioColor);
    dashboardFrame.drawPixel(117, 9, radioColor);
    dashboardFrame.drawPixel(121, 6, radioColor);
    dashboardFrame.drawFastVLine(122, 7, 2, radioColor);
    dashboardFrame.drawPixel(121, 9, radioColor);
    dashboardFrame.drawPixel(119, 7, radioColor);
    dashboardFrame.drawFastVLine(119, 8, 5, radioColor);
    dashboardFrame.drawLine(119, 11, 117, 16, radioColor);
    dashboardFrame.drawLine(119, 11, 121, 16, radioColor);
    dashboardFrame.drawFastHLine(118, 14, 3, radioColor);
    dashboardFrame.drawFastHLine(116, 16, 7, radioColor);
    dashboardFrame.setTextColor(radioColor);
    dashboardFrame.setCursor(130, 7);
    dashboardFrame.print("LoRa");

    {
        drawSensorCard(4, 22, "TEMP", hasData ? data.temperature : NAN, true, 0xFD68);
        drawSensorCard(82, 22, "HUM", hasData ? data.humidity : NAN, false, 0x4DDF);
        drawSensorCard(4, 63, "SOIL 1", hasData ? data.soil1 : NAN, false, green);
        drawSensorCard(82, 63, "SOIL 2", hasData ? data.soil2 : NAN, false, green);
    }
    dashboardFrame.fillRect(0, 103, 160, 11, bg);
    dashboardFrame.setTextSize(1);
    const uint16_t commandColor = status.command == CommandDisplayState::FAIL ? ST77XX_RED :
        (status.command == CommandDisplayState::ACK ? green :
        (status.command == CommandDisplayState::WAIT ? 0xFD68 : muted));
    dashboardFrame.setTextColor(commandColor);
    dashboardFrame.setCursor(5, 105);
    if (status.command == CommandDisplayState::NONE)
        dashboardFrame.print("CMD: --");
    else
    {
        char commandText[26];
        const char *state = status.command == CommandDisplayState::WAIT ? "WAIT" :
            (status.command == CommandDisplayState::ACK ? "ACK" : "FAIL");
        if (status.command == CommandDisplayState::WAIT)
            snprintf(commandText, sizeof(commandText), "Z%u %s: %s %u/%u", status.zone,
                     status.irrigationOn ? "ON" : "OFF", state, status.attempt, MAX_CMD_RETRIES);
        else
            snprintf(commandText, sizeof(commandText), "Z%u %s: %s", status.zone,
                     status.irrigationOn ? "ON" : "OFF", state);
        dashboardFrame.print(commandText);
    }
    // MQTT shares the command row; reserve its right edge for icon and label.
    const uint16_t mqttColor = status.mqttOnline && wifiOnline ? green : ST77XX_RED;
    // 16 x 10 outline cloud: rounded crown, soft shoulders and a flat base.
    static const uint8_t mqttCloudIcon[] PROGMEM = {
        0x03, 0xC0,
        0x04, 0x20,
        0x08, 0x10,
        0x38, 0x1C,
        0x40, 0x02,
        0x80, 0x01,
        0x80, 0x01,
        0x40, 0x02,
        0x3F, 0xFC,
        0x00, 0x00
    };
    dashboardFrame.drawBitmap(117, 104, mqttCloudIcon, 16, 10, mqttColor);
    dashboardFrame.setTextColor(mqttColor);
    dashboardFrame.setCursor(136, 105);
    dashboardFrame.print("MQTT");
    dashboardFrame.fillRect(0, 114, 160, 14, bg);
    dashboardFrame.fillCircle(7, 121, 2, radioColor);
    dashboardFrame.setTextSize(1);
    dashboardFrame.setTextColor(radioColor);
    dashboardFrame.setCursor(14, 118);
    dashboardFrame.print(!hasData ? "WAITING FOR DATA" : (stale ? "OLD" : "LIVE"));
    if (hasData)
    {
        char text[24];
        snprintf(text, sizeof(text), "Age Data:%lus", (unsigned long)(age / 1000));
        dashboardFrame.setTextColor(muted);
        dashboardFrame.setCursor(156 - strlen(text) * 6, 118);
        dashboardFrame.print(text);
    }
    static bool initialized = false;
    static char previousClock[6] = "";
    static uint16_t previousWifi = 0, previousRadio = 0, previousMqtt = 0;
    static DashboardStatus previousStatus;
    static bool previousHasData = false;
    static uint32_t previousAgeSeconds = 0;
    static char previousValues[4][20] = {};

    if (!initialized || strcmp(previousClock, clockText) != 0)
        flushDashboardRegion(0, 0, 40, 21);
    if (!initialized || previousWifi != wifiColor)
        flushDashboardRegion(40, 0, 60, 21);
    if (!initialized || previousRadio != radioColor)
        flushDashboardRegion(100, 0, 60, 21);

    const float values[] = {data.temperature, data.humidity, data.soil1, data.soil2};
    for (int i = 0; i < 4; ++i)
    {
        char valueText[20];
        if (hasData && isfinite(values[i]))
            snprintf(valueText, sizeof(valueText), "%.1f", values[i]);
        else
            snprintf(valueText, sizeof(valueText), "--");
        if (!initialized || strcmp(previousValues[i], valueText) != 0)
            flushDashboardRegion(i % 2 == 0 ? 4 : 82, i < 2 ? 22 : 63, 74, 38);
        strcpy(previousValues[i], valueText);
    }

    if (!initialized || previousStatus.command != status.command ||
        previousStatus.zone != status.zone || previousStatus.irrigationOn != status.irrigationOn ||
        (status.command == CommandDisplayState::WAIT && previousStatus.attempt != status.attempt))
        flushDashboardRegion(0, 103, 117, 11);
    if (!initialized || previousMqtt != mqttColor)
        flushDashboardRegion(117, 103, 43, 11);
    if (!initialized || previousHasData != hasData || previousRadio != radioColor ||
        (hasData && previousAgeSeconds != age / 1000))
        flushDashboardRegion(0, 114, 160, 14);

    strcpy(previousClock, clockText);
    previousWifi = wifiColor;
    previousRadio = radioColor;
    previousMqtt = mqttColor;
    previousStatus = status;
    previousHasData = hasData;
    previousAgeSeconds = age / 1000;
    initialized = true;

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
                if (xQueueReceive(mqttDataQueue, &data, 0) == pdPASS)
                {
                    /* MQTT PUBLISH */
                    publishData(data);
                }
            }
        }

        setMqttDisplayState(WiFi.status() == WL_CONNECTED && mqttClient.connected());
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

void DisplayTask(void *pvParameters)
{
    Serial.println("[RTOS] DisplayTask started");

    SensorData data = {};
    bool hasData = false;
    uint32_t receivedAt = 0;
    if (!dashboardFrame.getBuffer())
    {
        Serial.println("[DISPLAY] Framebuffer allocation failed");
        vTaskDelete(nullptr);
        return;
    }
    dashboardFrame.fillScreen(0x0862);
    tft.fillScreen(0x0862);
    updateDisplay(data, hasData, receivedAt);

    for (;;)
    {

        // Refresh connection and command status even when no sensor data arrives.
        if (xQueueReceive(displayQueue, &data, pdMS_TO_TICKS(200)) == pdPASS)
        {
            Serial.println("[DISPLAY] Update TFT");

            hasData = true;
            receivedAt = data.receivedAt;

        }
        updateDisplay(data, hasData, receivedAt);

        /* Nhường CPU */
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void setup()
{
    Serial.begin(115200);

    /* TFT INIT */
    spi.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
    tft.initR(INITR_BLACKTAB);
    tft.setRotation(1);
    tft.fillScreen(ST77XX_BLACK);
    tft.setTextWrap(false);
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(1);
    tft.setCursor(10, 20);
    tft.println("SMART FARM IoT");
    delay(1000);

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
    mqttDataQueue = xQueueCreate(5, sizeof(SensorData));
    displayQueue = xQueueCreate(5, sizeof(SensorData));

    if (commandQueue == NULL || mqttDataQueue == NULL || displayQueue == NULL)
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
        LoRaTask,         // Task function
        "LoRaTask",       // Task name
        4096,             // Stack size (bytes)
        NULL,             // Task parameters
        2,                // Task priority
        &loraTaskHandle); // Task handle

    xTaskCreate(
        MQTTTask,
        "MQTTTask",
        4096,
        NULL,
        1,
        &mqttTaskHandle);

    xTaskCreate(
        DisplayTask,
        "DisplayTask",
        4096,
        NULL,
        1,
        &displayTaskHandle);

    Serial.println("[RTOS] Tasks created!");
}

void loop()
{
    /* Arduino loopTask Block và nhường CPU 1s */
    vTaskDelay(pdMS_TO_TICKS(1000));
}
