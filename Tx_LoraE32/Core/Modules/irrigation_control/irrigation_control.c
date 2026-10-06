#include "irrigation_control.h"
#include "relay_driver.h"
#include "app_config.h"
#include "debug_console.h"
#include "system_manager.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>

/* Xử lý tưới nước */

static uint32_t last_manual_command_tick = 0;
static float soil_start_threshold[2] = {SOIL_START_THRESHOLD, SOIL_START_THRESHOLD};
static float soil_stop_threshold[2] = {SOIL_STOP_THRESHOLD, SOIL_STOP_THRESHOLD};

static uint8_t IrrigationControl_ParsePercent(const char *text, char delimiter, float *value)
{
    uint32_t whole = 0U;
    uint32_t fraction = 0U;
    uint32_t divisor = 1U;
    uint8_t has_digit = 0U;

    while (*text >= '0' && *text <= '9')
    {
        has_digit = 1U;
        whole = whole * 10U + (uint32_t)(*text - '0');
        text++;
    }

    if (*text == '.')
    {
        text++;
        while (*text >= '0' && *text <= '9')
        {
            has_digit = 1U;
            fraction = fraction * 10U + (uint32_t)(*text - '0');
            divisor *= 10U;
            text++;
        }
    }

    if (!has_digit || *text != delimiter || divisor == 0U) return 0U;
    *value = (float)whole + ((float)fraction / (float)divisor);
    return 1U;
}

static void IrrigationControl_ReleaseRelay(void)
{
    /* Không còn task nào sở hữu relay; đánh dấu để gửi telemetry trạng thái mới. */
    relay_owner = RELAY_OWNER_NONE;
    DebugConsole_Print("[RELAY] Owner released\r\n");
    telemetry_event_pending = 1;
}

static void IrrigationControl_UseAutoRelay(void)
{
    /* AUTO không được giành relay khi MANUAL đang sở hữu. */
    if (relay_owner != RELAY_OWNER_MANUAL)
    {
        relay_owner = RELAY_OWNER_AUTO;
        DebugConsole_Print("[RELAY] Owner = AUTO\r\n");
    }
}

static void IrrigationControl_UseManualRelay(void)
{
    /* Khi MANUAL tiếp quản: dừng hệ thống cũ và hủy FSM AUTO ngay. */
    RelayDriver_StopAll();
    irr_state = IRR_IDLE;
    relay_owner = RELAY_OWNER_MANUAL;
    DebugConsole_Print("[RELAY] Owner = MANUAL\r\n");
}

uint8_t IrrigationControl_RequestMeasurement(void)
{
    DebugConsole_Print("[AUTO] Request sensor measurement\r\n");

    irr_measure_pending = 1;

    uint32_t flags = osThreadFlagsSet(SensorTaskHandle, IRRIGATION_READ_SIGNAL);

    if ((flags & osFlagsError) != 0U)
    {
        /* Rollback pending nếu gửi signal cho SensorTask thất bại. */
        irr_measure_pending = 0;
        DebugConsole_Print("[ERROR] Irrigation sensor signal failed\r\n");
        return 0;
    }
    return 1;
}

void IrrigationControl_AutoUpdate(void)
{
    /* MANUAL luôn có quyền ưu tiên đối với relay. */
    if (relay_owner == RELAY_OWNER_MANUAL || irr_mode != MODE_AUTO) return;

    /* Sau khi STM32 reboot hoặc vừa chuyển lại AUTO, không được quyết định tưới
     * bằng dữ liệu sensor cũ/chưa hợp lệ. */
    if (!sensor_data_valid)
    {
        if (!irr_measure_pending) (void)IrrigationControl_RequestMeasurement();
        return;
    }

    /* AUTO IDLE: chủ động lấy dữ liệu sensor mới theo chu kỳ. */
    if (irr_state == IRR_IDLE && !irr_measure_pending &&
        (HAL_GetTick() - last_sensor_update_tick) >= AUTO_SENSOR_INTERVAL_MS)
    {
        sensor_data_valid = 0;
        (void)IrrigationControl_RequestMeasurement();
        return;
    }

    switch (irr_state)
    {
        /* =====================================
         * IDLE - CHỌN ZONE CẦN TƯỚI
         * ===================================== */
        case IRR_IDLE:
        {
            uint8_t zone1_dry = sm1 < soil_start_threshold[0];
            uint8_t zone2_dry = sm2 < soil_start_threshold[1];

            /* Không zone nào khô: bảo đảm toàn bộ relay đã OFF. */
            if (!zone1_dry && !zone2_dry)
            {
                if (relay_owner != RELAY_OWNER_NONE || valve1_state != VALVE_OFF || valve2_state != VALVE_OFF)
                {
                    RelayDriver_StopAll();
                    IrrigationControl_ReleaseRelay();
                }
                return;
            }

            /* Nếu cả hai zone cùng khô, ưu tiên zone có độ ẩm thấp hơn. */
            IrrigationControl_UseAutoRelay();
            irr_state_start = HAL_GetTick();
            if (zone1_dry && (!zone2_dry || sm1 <= sm2))
            {
                DebugConsole_Print(zone2_dry ? "[AUTO] Both dry -> Zone1 first\r\n" : "[AUTO] Select Zone1\r\n");
                irr_state = IRR_ZONE1_WATERING;
                RelayDriver_StartZone1();
            }
            else
            {
                DebugConsole_Print(zone1_dry ? "[AUTO] Both dry -> Zone2 first\r\n" : "[AUTO] Select Zone2\r\n");
                irr_state = IRR_ZONE2_WATERING;
                RelayDriver_StartZone2();
            }
            return;
        }

        case IRR_ZONE1_WATERING:
            /* Kết thúc xung tưới Zone 1 rồi chuyển sang thời gian nghỉ thấm. */
            if ((HAL_GetTick() - irr_state_start) >= WATER_PULSE_MS)
            {
                RelayDriver_StopCurrent(RELAY_OWNER_AUTO);
                irr_state = IRR_ZONE1_SOAK;
                irr_state_start = HAL_GetTick();
                telemetry_event_pending = 1;
            }
            break;

        case IRR_ZONE1_SOAK:
            /* Sau khi nước thấm, publish phase MEASURE trước khi đánh thức SensorTask.
             * Thứ tự này tránh SensorTask hoàn thành quá sớm và bỏ qua phase MEASURE. */
            if ((HAL_GetTick() - irr_state_start) >= SOAK_TIME_MS)
            {
                irr_state = IRR_ZONE1_MEASURE;
                irr_state_start = HAL_GetTick();
                telemetry_event_pending = 1;
                if (!IrrigationControl_RequestMeasurement())
                {
                    /* Gửi yêu cầu đo thất bại: quay lại SOAK để thử lại. */
                    irr_state = IRR_ZONE1_SOAK;
                    irr_state_start = HAL_GetTick();
                }
            }
            break;

        case IRR_ZONE1_MEASURE:
            /* Chờ dữ liệu mới và giữ phase MEASURE đủ lâu để ESP32/TFT quan sát được. */
            if (!irr_measure_pending && (HAL_GetTick() - irr_state_start) >= MEASURE_MIN_DISPLAY_MS)
            {
                if (sm1 >= soil_stop_threshold[0])
                {
                    /* Độ ẩm đạt ngưỡng: kết thúc chu kỳ và publish trạng thái ổn định. */
                    zone1_cycle = 0;
                    irr_state = IRR_IDLE;
                    irr_failed_zone = 0;
                    IrrigationControl_ReleaseRelay();
                }
                else if (++zone1_cycle >= MAX_IRRIGATION_CYCLE)
                {
                    /* Giữ FAILED cho đến khi có lệnh đổi mode rõ ràng. */
                    DebugConsole_Print("[ERROR] Zone1 irrigation failed\r\n");
                    RelayDriver_StopAll();
                    irr_failed_zone = 1;
                    irr_state = IRR_FAILED;
                    IrrigationControl_ReleaseRelay();
                }
                else
                {
                    /* Đất vẫn khô và chưa quá số chu kỳ: tưới thêm một xung. */
                    irr_state = IRR_ZONE1_WATERING;
                    irr_state_start = HAL_GetTick();
                    RelayDriver_StartZone1();
                }
            }
            break;

        case IRR_ZONE2_WATERING:
            /* Kết thúc xung tưới Zone 2 rồi chuyển sang thời gian nghỉ thấm. */
            if ((HAL_GetTick() - irr_state_start) >= WATER_PULSE_MS)
            {
                RelayDriver_StopCurrent(RELAY_OWNER_AUTO);
                irr_state = IRR_ZONE2_SOAK;
                irr_state_start = HAL_GetTick();
                telemetry_event_pending = 1;
            }
            break;

        case IRR_ZONE2_SOAK:
            /* Vào MEASURE trước khi gửi yêu cầu để phase không bị bỏ qua. */
            if ((HAL_GetTick() - irr_state_start) >= SOAK_TIME_MS)
            {
                irr_state = IRR_ZONE2_MEASURE;
                irr_state_start = HAL_GetTick();
                telemetry_event_pending = 1;
                if (!IrrigationControl_RequestMeasurement())
                {
                    /* Gửi yêu cầu đo thất bại: quay lại SOAK để thử lại. */
                    irr_state = IRR_ZONE2_SOAK;
                    irr_state_start = HAL_GetTick();
                }
            }
            break;

        case IRR_ZONE2_MEASURE:
            /* Chờ dữ liệu mới và giữ phase MEASURE đủ lâu để ESP32/TFT quan sát được. */
            if (!irr_measure_pending && (HAL_GetTick() - irr_state_start) >= MEASURE_MIN_DISPLAY_MS)
            {
                if (sm2 >= soil_stop_threshold[1])
                {
                    /* Độ ẩm đạt ngưỡng: kết thúc chu kỳ và publish trạng thái ổn định. */
                    zone2_cycle = 0;
                    irr_state = IRR_IDLE;
                    irr_failed_zone = 0;
                    IrrigationControl_ReleaseRelay();
                }
                else if (++zone2_cycle >= MAX_IRRIGATION_CYCLE)
                {
                    /* Giữ FAILED cho đến khi có lệnh đổi mode rõ ràng. */
                    DebugConsole_Print("[ERROR] Zone2 irrigation failed\r\n");
                    RelayDriver_StopAll();
                    irr_failed_zone = 2;
                    irr_state = IRR_FAILED;
                    IrrigationControl_ReleaseRelay();
                }
                else
                {
                    /* Đất vẫn khô và chưa quá số chu kỳ: tưới thêm một xung. */
                    irr_state = IRR_ZONE2_WATERING;
                    irr_state_start = HAL_GetTick();
                    RelayDriver_StartZone2();
                }
            }
            break;

        case IRR_FAILED:
            /* Yêu cầu đổi mode rõ ràng trước khi AUTO được phép thử lại. */
            return;

        default:
            /* State không hợp lệ: đưa output và FSM về trạng thái an toàn. */
            RelayDriver_StopAll();
            zone1_cycle = 0;
            zone2_cycle = 0;
            irr_state = IRR_IDLE;
            irr_failed_zone = 0;
            IrrigationControl_ReleaseRelay();
            break;
    }
}

uint8_t IrrigationControl_ProcessCommand(void)
{
    uint8_t success = 0;

    /* THRESHOLD CONTROL:
     *   <CMD,SEQ=x,THR,ZONE=1,START=35.0,STOP=55.0> */
    if (strstr(control_command, ",THR,") != NULL)
    {
        unsigned int zone = 0;
        float start_threshold = 0.0f;
        float stop_threshold = 0.0f;
        char *zone_text = strstr(control_command, "ZONE=");
        char *start_text = strstr(control_command, "START=");
        char *stop_text = strstr(control_command, "STOP=");

        if (zone_text != NULL && sscanf(zone_text, "ZONE=%u", &zone) == 1 &&
            start_text != NULL && IrrigationControl_ParsePercent(start_text + 6, ',', &start_threshold) &&
            stop_text != NULL && IrrigationControl_ParsePercent(stop_text + 5, '>', &stop_threshold) &&
            (zone == 1U || zone == 2U) && start_threshold >= 0.0f &&
            stop_threshold <= 100.0f && start_threshold < stop_threshold)
        {
            soil_start_threshold[zone - 1U] = start_threshold;
            soil_stop_threshold[zone - 1U] = stop_threshold;

            /* AUTO must use a fresh sample after its decision thresholds change. */
            if (irr_mode == MODE_AUTO)
            {
                sensor_data_valid = 0;
                last_sensor_update_tick = 0;
            }

            char threshold_message[96];
            snprintf(threshold_message, sizeof(threshold_message),
                     "[CONTROL] Zone%u threshold START=%.1f STOP=%.1f\r\n",
                     zone, start_threshold, stop_threshold);
            DebugConsole_Print(threshold_message);
            success = 1;
        }
        else
        {
            DebugConsole_Print("[CONTROL] Invalid irrigation threshold\r\n");
        }
    }
    /* MODE CONTROL:
     *   <CMD,SEQ=x,MODE=AUTO>
     *   <CMD,SEQ=x,MODE=MANUAL> */
    else if (strstr(control_command, "MODE=AUTO") != NULL)
    {
        DebugConsole_Print("[CONTROL] MODE -> AUTO\r\n");
        /* Dừng an toàn mọi hoạt động MANUAL/AUTO trước khi AUTO tiếp quản. */
        RelayDriver_StopAll();
        irr_mode = MODE_AUTO;
        irr_state = IRR_IDLE;
        irr_measure_pending = 0;
        irr_failed_zone = 0;
        /* Xóa dữ liệu cũ để AUTO đo lại sensor ngay và reset timer định kỳ. */
        sensor_data_valid = 0;
        last_sensor_update_tick = 0;
        zone1_cycle = 0;
        zone2_cycle = 0;
        last_manual_command_tick = 0;
        /* Chỉ trigger telemetry sau khi toàn bộ mode/state đã ổn định. */
        IrrigationControl_ReleaseRelay();
        success = 1;
    }
    else if (strstr(control_command, "MODE=MANUAL") != NULL)
    {
        DebugConsole_Print("[CONTROL] MODE -> MANUAL\r\n");
        /* Hủy AUTO ngay và đưa toàn bộ output về trạng thái OFF an toàn. */
        RelayDriver_StopAll();
        irr_mode = MODE_MANUAL;
        irr_state = IRR_IDLE;
        irr_measure_pending = 0;
        irr_failed_zone = 0;
        zone1_cycle = 0;
        zone2_cycle = 0;
        last_manual_command_tick = 0;
        IrrigationControl_ReleaseRelay();
        success = 1;
    }
    /* ZONE 1 */
    else if (strstr(control_command, "ZONE=1") != NULL)
    {
        if (irr_mode != MODE_MANUAL)
        {
            DebugConsole_Print("[CONTROL] ZONE1 rejected - not in MANUAL mode\r\n");
        }
        else if (strstr(control_command, "IRR=ON") != NULL)
        {
            /* Nếu Zone 1 đã được MANUAL tưới thì chỉ refresh safety timer,
             * không restart relay và bơm. Nếu Zone 2 đang chạy, ManualRelay
             * sẽ dừng hệ thống cũ trước khi chuyển zone. */
            if (!(relay_owner == RELAY_OWNER_MANUAL && valve1_state == VALVE_ON))
            {
                IrrigationControl_UseManualRelay();
                RelayDriver_StartZone1();
            }
            last_manual_command_tick = HAL_GetTick();
            success = 1;
        }
        else if (strstr(control_command, "IRR=OFF") != NULL)
        {
            if (relay_owner == RELAY_OWNER_MANUAL)
            {
                if (valve1_state == VALVE_ON)
                {
                    RelayDriver_StopCurrent(RELAY_OWNER_MANUAL);
                    IrrigationControl_ReleaseRelay();
                    last_manual_command_tick = 0;
                }
                /* Zone 1 vốn đã OFF: không làm ảnh hưởng Zone 2 đang tưới. */
                success = 1;
            }
            else if (relay_owner == RELAY_OWNER_NONE)
            {
                /* Relay vốn đã OFF nên vẫn trả SUCCESS. */
                last_manual_command_tick = 0;
                success = 1;
            }
        }
    }
    /* ZONE 2 */
    else if (strstr(control_command, "ZONE=2") != NULL)
    {
        if (irr_mode != MODE_MANUAL)
        {
            DebugConsole_Print("[CONTROL] ZONE2 rejected - not in MANUAL mode\r\n");
        }
        else if (strstr(control_command, "IRR=ON") != NULL)
        {
            /* Command lặp cho zone đang chạy chỉ refresh safety timer. */
            if (!(relay_owner == RELAY_OWNER_MANUAL && valve2_state == VALVE_ON))
            {
                IrrigationControl_UseManualRelay();
                RelayDriver_StartZone2();
            }
            last_manual_command_tick = HAL_GetTick();
            success = 1;
        }
        else if (strstr(control_command, "IRR=OFF") != NULL)
        {
            if (relay_owner == RELAY_OWNER_MANUAL)
            {
                if (valve2_state == VALVE_ON)
                {
                    RelayDriver_StopCurrent(RELAY_OWNER_MANUAL);
                    IrrigationControl_ReleaseRelay();
                    last_manual_command_tick = 0;
                }
                /* Zone 2 vốn đã OFF: không làm ảnh hưởng Zone 1 đang tưới. */
                success = 1;
            }
            else if (relay_owner == RELAY_OWNER_NONE)
            {
                last_manual_command_tick = 0;
                success = 1;
            }
        }
    }

    char message[80];
    /* Kiểm tra mức stack thấp nhất còn lại của IrrigationTask. */
    UBaseType_t watermark = uxTaskGetStackHighWaterMark(NULL);
    snprintf(message, sizeof(message), "[IrrigationTask STACK] Min free = %lu words\r\n", (unsigned long)watermark);
    DebugConsole_Print(message);
    return success;
}

void IrrigationControl_ManualSafetyCheck(void)
{
    /* Nếu lệnh OFF bị mất do LoRa/Wi-Fi/ESP32 lỗi, STM32 vẫn tự tắt bơm
     * và đóng van tại chỗ khi hết thời gian an toàn. */
    if (relay_owner == RELAY_OWNER_MANUAL && last_manual_command_tick != 0U &&
        (HAL_GetTick() - last_manual_command_tick) >= MANUAL_WATER_TIMEOUT_MS)
    {
        DebugConsole_Print("[SAFETY] Manual watering timeout -> force OFF\r\n");
        RelayDriver_StopCurrent(RELAY_OWNER_MANUAL);
        IrrigationControl_ReleaseRelay();
        last_manual_command_tick = 0;
    }
}

uint8_t IrrigationControl_IsActive(void)
{
    /* Xác định hệ thống có đang tưới theo FSM hoặc trạng thái output thực tế. */
    uint8_t auto_active = irr_state == IRR_ZONE1_WATERING || irr_state == IRR_ZONE1_SOAK || irr_state == IRR_ZONE1_MEASURE ||
                          irr_state == IRR_ZONE2_WATERING || irr_state == IRR_ZONE2_SOAK || irr_state == IRR_ZONE2_MEASURE;
    return auto_active || relay_owner != RELAY_OWNER_NONE || valve1_state == VALVE_ON || valve2_state == VALVE_ON || pump_state == PUMP_ON;
}

uint8_t IrrigationControl_GetActiveZone(void)
{
    /* Xác định zone active từ van thực tế và state machine AUTO. */
    if (irr_state == IRR_FAILED) return irr_failed_zone;
    if (valve1_state == VALVE_ON || irr_state == IRR_ZONE1_WATERING || irr_state == IRR_ZONE1_SOAK || irr_state == IRR_ZONE1_MEASURE) return 1;
    if (valve2_state == VALVE_ON || irr_state == IRR_ZONE2_WATERING || irr_state == IRR_ZONE2_SOAK || irr_state == IRR_ZONE2_MEASURE) return 2;
    return 0;
}

uint8_t IrrigationControl_GetPhase(void)
{
    /* Chuyển trạng thái nội bộ sang phase truyền qua LoRa:
     * 1=WATERING, 2=SOAK, 3=MEASURING, 4=FAILED. */
    if (irr_state == IRR_ZONE1_WATERING || irr_state == IRR_ZONE2_WATERING ||
        (relay_owner == RELAY_OWNER_MANUAL && (valve1_state == VALVE_ON || valve2_state == VALVE_ON))) return 1;
    if (irr_state == IRR_ZONE1_SOAK || irr_state == IRR_ZONE2_SOAK) return 2;
    if (irr_state == IRR_ZONE1_MEASURE || irr_state == IRR_ZONE2_MEASURE) return 3;
    if (irr_state == IRR_FAILED) return 4;
    return 0;
}

uint8_t IrrigationControl_GetCycle(void)
{
    /* Chu kỳ tưới chỉ có ý nghĩa trong AUTO; giá trị truyền đi bắt đầu từ 1. */
    if (irr_mode != MODE_AUTO) return 0;
    if (irr_state == IRR_FAILED) return MAX_IRRIGATION_CYCLE;
    if (IrrigationControl_GetActiveZone() == 1) return zone1_cycle + 1U;
    if (IrrigationControl_GetActiveZone() == 2) return zone2_cycle + 1U;
    return 0;
}
