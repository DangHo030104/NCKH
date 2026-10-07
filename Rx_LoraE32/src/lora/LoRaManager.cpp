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
static LoRaCommand pendingCommand = {};

/* ACTIVE IRRIGATION TELEMETRY */
static bool telemetryStreaming = false;
static unsigned long lastTelemetryAt = 0;

/* E32 transparent mode exposes a UART byte stream, not application frame
 * boundaries. Keep a small accumulator and extract each <...> frame so two
 * packets arriving close together cannot be mistaken for one payload. */
static String loraRxFrame = "";
static const size_t LORA_FRAME_MAX_LENGTH = 191;

/* RTOS QUEUES */
static QueueHandle_t commandQueue;
static QueueHandle_t commandStatusQueue;
static QueueHandle_t mqttDataQueue;
static QueueHandle_t displayQueue;

/* Phản hồi từng giai đoạn của CMD */
static void reportCommandStatus(CommandStatusCode status)
{
    CommandStatus report = {};
    report.status = status;
    report.command = pendingCommand;
    report.seq = waitingSeq;
    report.attempt = commandRetryCount + 1;

    if (xQueueSend(commandStatusQueue, &report, 0) != pdPASS)
    {
        Serial.println("[WARN] CommandStatusQueue FULL");
    }
}

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

    // LoRa frame: <D,seq,temp,hum,soil1,soil2,valve1,valve2,pump,mode,battery>
    String payload = frame.substring(3, frame.length() - 1);

    char buffer[128];

    // Copy payload to buffer for tokenization
    payload.toCharArray(buffer, sizeof(buffer));

    float value[10];

    int index = 0;

    // Tokenize the buffer using comma as the delimiter
    char *token = strtok(buffer, ",");

    while (token != NULL && index < 10)
    {
        value[index++] = atof(token); // Convert token to float and store in value array
        token = strtok(NULL, ",");    // Get the next token
    }

    if (index != 10 || token != NULL)
        return false;

    data.seq = value[0];
    data.temperature = value[1];
    data.humidity = value[2];
    data.soil1 = value[3];
    data.soil2 = value[4];
    data.valve1 = value[5] ? VALVE_ON : VALVE_OFF;
    data.valve2 = value[6] ? VALVE_ON : VALVE_OFF;
    data.pump = value[7] ? PUMP_ON : PUMP_OFF;
    data.irrigationMode = value[8] ? MODE_AUTO : MODE_MANUAL;
    data.batteryPercent = (uint8_t)value[9];
    /* Khi parse DATA, các trường streaming được đặt về mặc định */
    data.activeZone = 0;
    data.irrigationPhase = IRRIGATION_PHASE_IDLE;
    data.irrigationCycle = 0;
    data.streaming = false;
    data.receivedAt = millis(); // Ghi nhận thời điểm nhận frame DATA để xác định frame cũ hay mới.

    return true;
}

static bool parseTelemetryFrame(const String &frame, SensorData &data)
{
    if (!frame.startsWith("<T,") || !frame.endsWith(">"))
        return false;

    // LoRa frame: <T,seq,temp,hum,soil1,soil2,valve1,valve2,pump,mode,battery,zone,phase,cycle>
    String payload = frame.substring(3, frame.length() - 1);

    char buffer[160];

    if (payload.length() >= sizeof(buffer))
        return false;

    // Copy payload to buffer for tokenization
    payload.toCharArray(buffer, sizeof(buffer));

    float value[13];
    int index = 0;
    char *token = strtok(buffer, ",");
    while (token != NULL && index < 13)
    {
        value[index++] = atof(token); // Convert token to float and store in value array
        token = strtok(NULL, ",");    // Get the next token
    }

    if (index != 13 || token != NULL)
        return false;

    const int zone = (int)value[10];
    const int phase = (int)value[11];
    const int cycle = (int)value[12];

    if (zone < 0 || zone > 2 || phase < IRRIGATION_PHASE_IDLE ||
        phase > IRRIGATION_PHASE_FAILED || cycle < 0 || cycle > 255)
        return false;

    data.seq = (uint32_t)value[0];
    data.temperature = value[1];
    data.humidity = value[2];
    data.soil1 = value[3];
    data.soil2 = value[4];
    data.valve1 = value[5] ? VALVE_ON : VALVE_OFF;
    data.valve2 = value[6] ? VALVE_ON : VALVE_OFF;
    data.pump = value[7] ? PUMP_ON : PUMP_OFF;
    data.irrigationMode = value[8] ? MODE_AUTO : MODE_MANUAL;
    data.batteryPercent = (uint8_t)value[9];
    data.activeZone = (uint8_t)zone;
    data.irrigationPhase = (IrrigationPhase)phase;
    data.irrigationCycle = (uint8_t)cycle;
    /* FAILED is a terminal snapshot. Only active irrigation phases keep the
     * live stream open and suppress normal REQ polling. */
    data.streaming = data.irrigationPhase == IRRIGATION_PHASE_WATERING ||
                     data.irrigationPhase == IRRIGATION_PHASE_SOAK ||
                     data.irrigationPhase == IRRIGATION_PHASE_MEASURING;
    data.receivedAt = millis(); // Lưu thời điểm nhận frame DATA để xác định frame cũ hay mới.
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
    vTaskDelay(pdMS_TO_TICKS(20));

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
    vTaskDelay(pdMS_TO_TICKS(20));

    Serial.print("Waiting ACK SEQ: ");
    Serial.println(waitingSeq);

    loraState = WAIT_CMD_ACK;
    loraStateStartedAt = millis();

    reportCommandStatus(commandRetryCount == 0 ? COMMAND_STATUS_SENT : COMMAND_STATUS_RETRYING);
}

static void sendPendingCommand(const LoRaCommand &cmd)
{
    pendingCommand = cmd;
    sequenceNumber++;

    if (sequenceNumber == 0)
    {
        sequenceNumber = 1;
    }

    waitingSeq = sequenceNumber; // Ghi nhớ seq mà ACK phải chứa.

    /* Ví dụ: pendingCommand: ZONE=1,IRR=ON => <CMD,SEQ=25,ZONE=1,IRR=ON> */
    if (cmd.type == COMMAND_MODE)
    {
        /* STM32 accepts: <CMD,SEQ=x,MODE=AUTO|MANUAL> */
        pendingCommandFrame = "<CMD,SEQ=" + String(sequenceNumber) + ",MODE=" +
                              String(cmd.mode == MODE_AUTO ? "AUTO" : "MANUAL") + ">";
    }
    else if (cmd.type == COMMAND_IRRIGATION)
    {
        /* STM32 accepts: <CMD,SEQ=x,ZONE=1|2,IRR=ON|OFF> */
        pendingCommandFrame = "<CMD,SEQ=" + String(sequenceNumber) + ",ZONE=" +
                              String(cmd.zone) + ",IRR=" + String(cmd.irr ? "ON" : "OFF") + ">";
    }
    else   
    {
        /* STM32 accepts: <CMD,SEQ=x,THR,ZONE=1|2,START=xx.x,STOP=yy.y> */
        pendingCommandFrame = "<CMD,SEQ=" + String(sequenceNumber) + ",THR,ZONE=" +
                              String(cmd.zone) + ",START=" + String(cmd.startThreshold, 1) +
                              ",STOP=" + String(cmd.stopThreshold, 1) + ">";
    }

    commandRetryCount = 0;
    transmitPendingCommandFrame();
}

static void handleAckFrame(const String &frame)
{
    uint32_t receivedSeq = getSeq(frame);

    if (loraState == WAIT_CMD_ACK && receivedSeq == waitingSeq)
    {
        Serial.println("ACK MATCH -> COMMAND SUCCESS");

        reportCommandStatus(COMMAND_STATUS_SUCCESS);

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

    if (data.seq != waitingSeq)
    {
        Serial.println("DATA SEQ INVALID");
        return;
    }

    Serial.println("DATA SEQ MATCH");

    xQueueOverwrite(mqttDataQueue, &data);
    xQueueOverwrite(displayQueue, &data);

    Serial.println("[QUEUE] DATA SENT");

    loraState = LORA_IDLE;
}

static void handleTelemetryFrame(const String &frame)
{
    SensorData data = {};
    if (!parseTelemetryFrame(frame, data))
    {
        Serial.println("INVALID TELEMETRY -> IGNORED");
        return;
    }

    const bool wasStreaming = telemetryStreaming;
    lastTelemetryAt = millis();          // Cập nhật thời điểm nhận frame telemetry mới nhất
    telemetryStreaming = data.streaming; // Cập nhật trạng thái streaming

    /* Nếu đang streaming mà nhận được frame telemetry mới, không reset lastRequest
     * để tránh gửi REQ ngay sau đó. Chỉ reset lastRequest khi streaming kết thúc. */
    if (wasStreaming && !telemetryStreaming)
    {
        // Start a fresh normal polling interval after final telemetry.
        lastRequest = lastTelemetryAt;
    }

    /* Cập nhật dữ liệu telemetry vào mqttDataQueue và displayQueue */
    xQueueOverwrite(mqttDataQueue, &data);
    xQueueOverwrite(displayQueue, &data);

    Serial.println("[QUEUE] LIVE TELEMETRY UPDATED");
}

/* Phân loại frame LoRa nhận được */
static void processLoRaFrame(const String &frame)
{
    // Telemetry is asynchronous and must not complete/cancel a pending REQ or CMD.
    if (frame.startsWith("<T,"))
    {
        handleTelemetryFrame(frame);
        return;
    }

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

/* Xử lý frame LoRa từ UART */
static void receiveLoRaFrames(void)
{
    while (Serial2.available() > 0)
    {
        const char c = (char)Serial2.read();

        /* Nếu nhận được ký tự '<', bắt đầu một khung mới */
        if (c == '<')
        {
            loraRxFrame = "<";
            continue;
        }

        /* Nếu khung nhận được rỗng, bỏ qua */
        if (loraRxFrame.length() == 0)
            continue;

        /* Nếu khung nhận được quá dài, bỏ qua */
        if (loraRxFrame.length() >= LORA_FRAME_MAX_LENGTH)
        {
            loraRxFrame = "";
            Serial.println("[LORA] RX frame overflow -> discarded");
            continue;
        }

        loraRxFrame += c; // Thêm ký tự vào frame

        /* Nếu nhận được ký tự '>', kết thúc khung */
        if (c == '>')
        {
            Serial.print("\nLoRa RX FRAME: ");
            Serial.println(loraRxFrame);
            processLoRaFrame(loraRxFrame);
            loraRxFrame = ""; // Reset frame for next frame
        }
    }
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
            /* VD: Nếu đang retry ON cũ nhưng đã có OFF mới, ESP32 sẽ: 
             * 1. Hủy retry lệnh ON  
             * 2. Báo lệnh cũ FAILED.
             * 3. Trở lại LORA_IDLE.
             * 4. Gửi lệnh OFF mới. */
            if (pendingCommand.type == COMMAND_IRRIGATION &&
                pendingCommand.revision < readLatestZoneCommand(pendingCommand.zone))
            {
                Serial.println("\n[LORA] Newer zone command -> cancel retry of stale command");
                reportCommandStatus(COMMAND_STATUS_FAILED);
                pendingCommandFrame = "";
                commandRetryCount = 0;
                loraState = LORA_IDLE;
                return;
            }

            Serial.println("\nACK TIMEOUT -> RETRY COMMAND");
            
            /* Telemetry STM32 phát theo chu kỳ 1s. 
             * Thêm độ lệch ngẫu nhiên 80–280 ms khi retry để tránh xung đột với telemetry.*/
             vTaskDelay(pdMS_TO_TICKS(random(CMD_RETRY_JITTER_MIN_MS, CMD_RETRY_JITTER_MAX_MS + 1)));
            
             transmitPendingCommandFrame();
        }
        else
        {
            Serial.println("\nCOMMAND FAILED -> MAX RETRIES REACHED");
            reportCommandStatus(COMMAND_STATUS_FAILED);
            pendingCommandFrame = "";
            commandRetryCount = 0;

            loraState = LORA_IDLE;
        }
    }
}

void LoRaManager_Begin(QueueHandle_t commands, QueueHandle_t commandStatus,
                       QueueHandle_t mqttData, QueueHandle_t displayData)
{
    commandQueue = commands;
    commandStatusQueue = commandStatus;
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
        /* Read the E32 UART as a stream and process every complete <...> frame. */
        receiveLoRaFrames();

        /* TIMEOUT  */
        handleLoraTimeouts();

        /* Lệnh CMD ưu tiên hơn REQ sensor đang chờ DATA. DATA đến muộn
         * sẽ bị loại bằng SEQ sau khi CMD mới đã được gửi. */
        if (loraState == WAIT_DATA && uxQueueMessagesWaiting(commandQueue) > 0)
        {
            Serial.println("[LORA] Pending CMD -> cancel WAIT_DATA");
            loraState = LORA_IDLE;
        }

        if (telemetryStreaming && millis() - lastTelemetryAt >= TELEMETRY_TIMEOUT_MS)
        {
            telemetryStreaming = false;
            Serial.println("[LORA] Telemetry timeout -> polling resumed");
        }

        /* MASTER SCHEDULER */
        if (loraState == LORA_IDLE)
        {
            /* CMD ưu tiên hơn polling */
            if (xQueueReceive(commandQueue, &cmd, 0) == pdPASS)
            {
                if (cmd.type == COMMAND_IRRIGATION && cmd.zone >= 1 && cmd.zone <= 2 &&
                    cmd.revision < readLatestZoneCommand(cmd.zone))
                {
                    Serial.println("[LORA] Stale zone command skipped");
                }
                else
                {
                    sendPendingCommand(cmd);
                }
            }

            /* Không có CMD -> Polling sensor */
            else if (!telemetryStreaming && millis() - lastRequest >= REQUEST_INTERVAL)
            {
                lastRequest = millis();
                sendRequest();
            }
        }

        /* Nhường CPU cho task khác 1ms */
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
