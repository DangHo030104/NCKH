#include "LoRaManager.h"
#include "../config/AppConfig.h"
#include "../common/SharedState.h"
#include <Arduino.h>
#include <LoRa_E32.h>
#include <math.h>

/* LORA STATE MACHINE */
typedef enum
{
    LORA_IDLE,
    WAIT_DATA,
    WAIT_CMD_ACK
} LoRaState;

static LoRaState loraState = LORA_IDLE;

/* LORA E32 */
static LoRa_E32 e32ttl100(TX_PIN, RX_PIN, &Serial2, AUX_PIN, M0_PIN, M1_PIN, UART_BPS_RATE_9600, SERIAL_8N1);

/* MASTER REQUEST (ESP32 -> STM32) */
static unsigned long lastRequest = 0;

/* SEQUENCE NUMBER */
static uint32_t sequenceNumber = 0;
static uint32_t waitingSeq = 0;

/* PENDING COMMAND BUFFER */
static String pendingCommandFrame = "";      // Command frame đang được gửi đi, chờ ACK
static unsigned long loraStateStartedAt = 0; // Lưu thời điểm bắt đầu chờ DATA hoặc ACK
static uint8_t commandRetryCount = 0;

/* RTOS QUEUES */
static QueueHandle_t commandQueue;
static QueueHandle_t mqttDataQueue;
static QueueHandle_t displayQueue;

static void printAuxState(void)
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

static bool parseDataFrame(const String &frame, SensorData &data)
{

    if (!frame.startsWith("<D,") || !frame.endsWith(">"))
        return false;

    // LoRa frame: <D,seq,temp,hum,soil1,soil2,valve1,valve2,mode,battery> -> Remove <D, and > from the frame
    String payload = frame.substring(3, frame.length() - 1);

    char buffer[128];

    // Copy payload to buffer for tokenization
    payload.toCharArray(buffer, sizeof(buffer));

    char *token;

    float value[9];

    int index = 0;

    // Tokenize the buffer using comma as the delimiter
    token = strtok(buffer, ",");

    while (token != NULL && index < 9)
    {
        value[index++] = atof(token);

        token = strtok(NULL, ",");
    }

    if (index != 9)
        return false;

    data.seq = value[0];
    data.temperature = value[1];
    data.humidity = value[2];
    data.soil1 = value[3];
    data.soil2 = value[4];
    data.valve1 = value[5] ? VALVE_ON : VALVE_OFF;
    data.valve2 = value[6] ? VALVE_ON : VALVE_OFF;
    data.irrigationMode = value[7] ? MODE_AUTO : MODE_MANUAL;
    data.batteryPercent = (uint8_t)value[8];
    data.receivedAt = millis();

    return true;
}

/* Get sequence number from frame */
static uint32_t getSeq(const String &frame)
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

static bool handleDataFrame(const String &frame, SensorData &data)
{
    return parseDataFrame(frame, data);
}

static void sendRequest(void)
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

static void transmitPendingCommandFrame(void)
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

static void sendPendingCommand(const LoRaCommand &cmd)
{
    sequenceNumber++;

    if (sequenceNumber == 0)
    {
        sequenceNumber = 1;
    }

    waitingSeq = sequenceNumber;    // Ghi nhớ seq mà ACK phải chứa.

    /* Ví dụ: pendingCommand: ZONE=1,IRR=ON => <CMD,SEQ=25,ZONE=1,IRR=ON> */
    pendingCommandFrame = "<CMD,SEQ=" + String(sequenceNumber) + "," + "ZONE=" + String(cmd.zone) + ",IRR=" + String(cmd.irr ? "ON" : "OFF") + ">";

    commandRetryCount = 0;
    transmitPendingCommandFrame();
}

static void handleAckFrame(const String &frame)
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

static void handleReceivedData(const String &frame)
{
    SensorData data = {};

    if (!parseDataFrame(frame, data))
    {
        Serial.println("INVALID DATA PAYLOAD -> IGNORED");
        return;
    }

    Serial.println("DATA PARSE OK");

    Serial.print("SEQ: ");
    Serial.println(data.seq);

    if(data.seq != waitingSeq)
    {
        Serial.println("DATA SEQ INVALID");
        return;
    }

    Serial.println("DATA SEQ MATCH");

    xQueueSend(mqttDataQueue, &data, 0);

    xQueueSend(displayQueue, &data, 0);

    Serial.println("[QUEUE] DATA SENT");

    loraState = LORA_IDLE;
}

/* Phân loại frame LoRa nhận được */
static void processLoRaFrame(const String &frame)
{
    // DATA
    if (frame.startsWith("<D"))
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

static void handleLoraTimeouts(void)
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

void LoRaManager_Begin(QueueHandle_t commands, QueueHandle_t mqttData, QueueHandle_t displayData)
{
    commandQueue = commands;
    mqttDataQueue = mqttData;
    displayQueue = displayData;
    /* LoRa E32 */
    e32ttl100.begin();
    Serial.println("LoRa Receiver Started");
}

void LoRaManager_Run(void *pvParameters)
{
    Serial.println("[RTOS] LoRaTask started!");

    LoRaCommand cmd;

    // Gửi request đầu tiên ngay khi LoRaTask bắt đầu, thay vì chờ đủ 30 giây.
    lastRequest = millis() - REQUEST_INTERVAL;

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