#include "MQTTManager.h"
#include "../config/AppConfig.h"
#include "../common/SharedState.h"
#include "../wifi/WiFiManager.h"
#include "../system/SystemManager.h"
#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

/* MQTT CLIENT */
static WiFiClientSecure secureClient;         // Tạo kết nối TCP có mã hóa TLS
static PubSubClient mqttClient(secureClient); // Tạo MQTTClient sử dụng kết nối TCP đã mã hóa TLS.

static unsigned long lastMqttReconnectAttempt = 0;

/* RTOS QUEUES */
static QueueHandle_t commandQueue;
static QueueHandle_t mqttDataQueue;

/* MQTT Client tự gọi hàm này khi nhận message từ MQTT Server */
static void mqttCallback(char *topic, byte *payload, unsigned int length)
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

    /* MODE command: {"mode":"AUTO"} or {"mode":"MANUAL"} */
    const char *mode = doc["mode"];     // Lấy trường mode (AUTO hoặc MANUAL)

    if (mode != nullptr)
    {
        LoRaCommand modeCommand = {};
        modeCommand.type = COMMAND_MODE;

        if (strcmp(mode, "AUTO") == 0)
        {
            modeCommand.mode = MODE_AUTO;
        }
        else if (strcmp(mode, "MANUAL") == 0)
        {
            modeCommand.mode = MODE_MANUAL;
        }
        else
        {
            Serial.print("[ERROR] Invalid mode: ");
            Serial.println(mode);
            return;
        }

        if (xQueueSend(commandQueue, &modeCommand, 0) == pdPASS)
        {
            Serial.print("[MQTT] CMD queued: MODE=");
            Serial.println(mode);
        }
        else
        {
            Serial.println("[ERROR] CommandQueue FULL");
        }

        return;
    }

    /* GET RELAY + STATE */
    int relay = doc["relay"] | 0;     // Lấy trường relay (1 hoặc 2)
    const char *state = doc["state"]; // Lấy trường state (ON/OFF)

    if ((relay != 1 && relay != 2) || state == nullptr)
    {
        Serial.println("Invalid MQTT command");
        return;
    }

    /* CREATE LORA COMMAND */
    LoRaCommand cmd = {};
    cmd.type = COMMAND_IRRIGATION;
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

static void reconnectMQTT(void)
{
    if (!WiFiManager_IsConnected())
        return;

    if (mqttClient.connected())
        return;

    setMqttDisplayState(false);

    if (lastMqttReconnectAttempt != 0 && millis() - lastMqttReconnectAttempt < MQTT_RECONNECT_INTERVAL)
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

static void publishData(const SensorData &data)
{
    JsonDocument doc; // Tạo JSON document.

    doc["T"] = data.temperature;
    doc["H"] = data.humidity;
    doc["SM1"] = data.soil1;
    doc["SM2"] = data.soil2;
    doc["seq"] = data.seq;
    doc["valve1"] = (data.valve1 == VALVE_ON) ? "ON" : "OFF";
    doc["valve2"] = (data.valve2 == VALVE_ON) ? "ON" : "OFF";
    doc["mode"] = (data.irrigationMode == MODE_AUTO) ? "AUTO" : "MANUAL";
    
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

void MQTTManager_Begin(QueueHandle_t commands, QueueHandle_t mqttData)
{
    commandQueue = commands;
    mqttDataQueue = mqttData;
    /* MQTT */
    secureClient.setInsecure();                   // Bỏ kiểm tra CA, TLS vẫn mã hóa
    mqttClient.setServer(mqtt_server, mqtt_port); // Cấu hình địa chỉ và cổng broker.
    mqttClient.setCallback(mqttCallback);         // Đăng ký hàm xử lý callback khi nhận message từ MQTT.
}

void MQTTManager_Run(void *pvParameters)
{
    Serial.println("[RTOS] MQTTTask started");

    SensorData data;

    for (;;)
    {
        if (WiFiManager_Maintain())
        {
            /* Đồng bộ thời gian */
            SystemManager_SyncTime();
            /* Cho phép MQTT reconnect ngay sau khi WiFi vừa trở lại */
            lastMqttReconnectAttempt = 0;
        }
        if (!WiFiManager_IsConnected() && mqttClient.connected())
        {
            /* Khi WiFi mất thì đóng MQTT session cũ */
            mqttClient.disconnect();
            Serial.println("[MQTT] Disconnected due to WiFi loss");
        }

        /* Chỉ kết nối và duy trì MQTT khi WiFi Ready */
        if (WiFiManager_IsConnected())
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

        setMqttDisplayState(WiFiManager_IsConnected() && mqttClient.connected());
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}
