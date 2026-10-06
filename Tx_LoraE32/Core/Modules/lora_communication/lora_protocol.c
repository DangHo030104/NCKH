#include "lora_protocol.h"
#include "lora_e32.h"
#include "app_config.h"
#include "debug_console.h"
#include "irrigation_control.h"
#include "system_manager.h"
#include <stdio.h>
#include <string.h>

/* ----- Xử lý Frame ----- */

/* Duplicate CMD protection: lưu command đã thực thi thành công gần nhất. */
static uint32_t last_command_sequence = 0;
static uint32_t last_command_tick = 0;
static uint8_t last_command_valid = 0;
static char last_command[64];
/* Sequence riêng của streaming, độc lập với sequence của REQ/DATA. */
static uint32_t telemetry_sequence = 0;

static void LoRaProtocol_SendAck(uint32_t sequence)
{
    char frame[40];

    /* Chuyển E32 từ Power-Saving sang Normal trước khi phát ACK. */
    LoRaE32_SetNormalMode();
    if (!LoRaE32_WaitReady(100)) return;

    snprintf(frame, sizeof(frame), "<ACK,SEQ=%lu>", sequence);

    DebugConsole_Print("[LORA TX] ");
    DebugConsole_Print(frame);
    DebugConsole_Print("\r\n");

    HAL_UART_Transmit(&huart1, (uint8_t *)frame, strlen(frame), 500);
    last_lora_tx_tick = HAL_GetTick();

    /* Chờ E32 phát RF hoàn tất trước khi Task tiếp tục. */
    LoRaE32_WaitReady(500U);
}

static void LoRaProtocol_SendData(uint32_t sequence)
{
    char frame[128];

    /* Chuyển E32 từ Power-Saving sang Normal trước khi phát DATA. */
    LoRaE32_SetNormalMode();
    if (!LoRaE32_WaitReady(100U)) return;

    /* Cho ESP32 đủ thời gian chuyển từ Mode WAKE-UP sang Normal RX. */
    osDelay(30);

    /* <DATA,seq,temp,hum,soil1,soil2,valve1,valve2,pump,mode,battery> */
    snprintf(frame, sizeof(frame), "<D,%lu,%d,%d,%.2f,%.2f,%d,%d,%d,%d,%d>",
             sequence, temp, humi, sm1, sm2,
             valve1_state == VALVE_ON, valve2_state == VALVE_ON,
             pump_state == PUMP_ON, irr_mode == MODE_AUTO, battery_percent);

    DebugConsole_Print("[LORA TX] ");
    DebugConsole_Print(frame);
    DebugConsole_Print("\r\n");

    HAL_UART_Transmit(&huart1, (uint8_t *)frame, strlen(frame), 500);
    last_lora_tx_tick = HAL_GetTick();

    (void)LoRaE32_WaitReady(500);
}

static void LoRaProtocol_ProcessRequest(char *frame)
{
    uint32_t seq = 0;

    /* 1. Parse và kiểm tra frame REQ. */
    if (sscanf(frame, "<REQ,SEQ=%lu>", &seq) != 1)
    {
        DebugConsole_Print("[ERROR] INVALID REQ\r\n");
        return;
    }

    /* 2. Yêu cầu SensorTask tạo một snapshot mới. */
    uint32_t flags = osThreadFlagsSet(SensorTaskHandle, SENSOR_READ_SIGNAL);
    if ((flags & osFlagsError) != 0) return;

    /* 3. Chờ SensorTask báo dữ liệu đã sẵn sàng. */
    flags = osThreadFlagsWait(SENSOR_READY_SIGNAL, osFlagsWaitAny, 2000U);
    if ((flags & osFlagsError) != 0U || !(flags & SENSOR_READY_SIGNAL))
    {
        DebugConsole_Print("[ERROR] SENSOR TASK TIMEOUT\r\n");
        return;
    }

    /* 4. Gửi DATA có cùng sequence với REQ. */
    LoRaProtocol_SendData(seq);
}

static void LoRaProtocol_ProcessCommand(char *frame)
{
    uint32_t seq = 0;

    /* 1. Tách và kiểm tra SEQ của command. */
    char *sequence_text = strstr(frame, "SEQ=");
    if (sequence_text == NULL || sscanf(sequence_text, "SEQ=%lu", &seq) != 1)
    {
        DebugConsole_Print("[ERROR] CMD INVALID SEQ\r\n");
        return;
    }

    /* 2. ESP32 sẽ gửi lại đúng frame và SEQ khi mất ACK.
     * Khi đó không thực thi relay/mode lần nữa, chỉ gửi lại ACK.
     * So sánh cả SEQ lẫn nội dung để vẫn an toàn khi ESP32 reboot và reset SEQ. */
    if (last_command_valid && last_command_sequence == seq &&
        strcmp(last_command, frame) == 0 && (HAL_GetTick() - last_command_tick) <= CMD_DUPLICATE_MS)
    {
        DebugConsole_Print("[CMD] Duplicate -> ACK only\r\n");
        LoRaProtocol_SendAck(seq);
        return;
    }

    /* 3. Copy command sang vùng dữ liệu dùng chung. */
    strncpy(control_command, frame, sizeof(control_command) - 1U);
    control_command[sizeof(control_command) - 1U] = '\0';

    /* 4. Đánh thức IrrigationTask để thực thi command. */
    uint32_t flags = osThreadFlagsSet(IrrigationTaskHandle, CONTROL_EXEC_SIGNAL);
    if ((flags & osFlagsError) != 0U) return;

    /* 5. Chờ kết quả thực thi trước khi quyết định gửi ACK. */
    flags = osThreadFlagsWait(CONTROL_OK_SIGNAL | CONTROL_ERROR_SIGNAL, osFlagsWaitAny, 2000);
    if ((flags & osFlagsError) != 0U) return;

    if (flags & CONTROL_OK_SIGNAL)
    {
        /* Lưu command trước khi gửi ACK. Nếu ACK bị mất, lần retry chỉ gửi lại ACK. */
        last_command_sequence = seq;
        last_command_tick = HAL_GetTick();
        last_command_valid = 1;
        strncpy(last_command, frame, sizeof(last_command) - 1U);
        last_command[sizeof(last_command) - 1U] = '\0';

        LoRaProtocol_SendAck(seq);
    }
    else if (flags & CONTROL_ERROR_SIGNAL)
    {
        DebugConsole_Print("[ERROR] CONTROL FAILED - NO ACK\r\n");
    }
}

void LoRaProtocol_ProcessFrame(char *frame)
{
    /* Phân loại frame hoàn chỉnh do UART ISR chuyển lên. */
    if (strstr(frame, "<REQ,") != NULL)
    {
        LoRaProtocol_ProcessRequest(frame);
    }
    else if (strstr(frame, "<CMD,") != NULL)
    {
        LoRaProtocol_ProcessCommand(frame);
    }
    else
    {
        DebugConsole_Print("[ERROR] UNKNOWN FRAME\r\n");
    }
}

void LoRaProtocol_SendTelemetry(void)
{
    char frame[128];
    LoRaE32_SetNormalMode();
    if (!LoRaE32_WaitReady(100U)) return;

    /* Sequence 0 được bỏ qua để dễ nhận biết dữ liệu chưa khởi tạo. */
    if (++telemetry_sequence == 0) telemetry_sequence = 1;

    /* <Telemetry,seq,temp,hum,soil1,soil2,v1,v2,pump,mode,battery,zone,phase,cycle> */
    snprintf(frame, sizeof(frame), "<T,%lu,%d,%d,%.2f,%.2f,%d,%d,%d,%d,%d,%d,%d,%d>",
             telemetry_sequence, temp, humi, sm1, sm2,
             valve1_state == VALVE_ON, valve2_state == VALVE_ON,
             pump_state == PUMP_ON, irr_mode == MODE_AUTO, battery_percent,
             IrrigationControl_GetActiveZone(), IrrigationControl_GetPhase(),
             IrrigationControl_GetCycle());

    HAL_UART_Transmit(&huart1, (uint8_t *)frame, strlen(frame), 500U);
    last_lora_tx_tick = HAL_GetTick();

    LoRaE32_WaitReady(500U);
}
