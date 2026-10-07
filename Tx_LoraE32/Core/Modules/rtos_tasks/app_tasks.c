#include "app_tasks.h"
#include "app_config.h"
#include "debug_console.h"
#include "irrigation_control.h"
#include "lora_e32.h"
#include "lora_protocol.h"
#include "power_manager.h"
#include "rtc_service.h"
#include "sensor_manager.h"
#include "system_manager.h"
#include "telemetry_manager.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>

#if WAKE_DEBUG_LOG_ENABLE
static const char *AppTasks_GetIrrigationStateName(void)
{
    switch (irr_state)
    {
    case IRR_IDLE:            return "IDLE";
    case IRR_ZONE1_WATERING:  return "ZONE1_WATERING";
    case IRR_ZONE1_SOAK:      return "ZONE1_SOAK";
    case IRR_ZONE1_MEASURE:   return "ZONE1_MEASURE";
    case IRR_ZONE2_WATERING:  return "ZONE2_WATERING";
    case IRR_ZONE2_SOAK:      return "ZONE2_SOAK";
    case IRR_ZONE2_MEASURE:   return "ZONE2_MEASURE";
    case IRR_FAILED:          return "FAILED";
    default:                  return "UNKNOWN";
    }
}
#endif

static void AppTasks_LogWakeState(const char *stage)
{
#if WAKE_DEBUG_LOG_ENABLE
    RTC_TimeTypeDef time = {0};
    RTC_DateTypeDef date = {0};
    char message[224];

    HAL_RTC_GetTime(&hrtc, &time, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &date, RTC_FORMAT_BIN);

    snprintf(message, sizeof(message),
             "[WAKE-TEST] %s | RTC=%02u:%02u:%02u | MODE=%s | STATE=%s | AUX=%s | "
             "flags(RTC=%u,LORA=%u,FRAME=%u,RX=%u) | IRQ(RTC=%lu,LORA=%lu)\r\n",
             stage,
             (unsigned int)time.Hours,
             (unsigned int)time.Minutes,
             (unsigned int)time.Seconds,
             irr_mode == MODE_AUTO ? "AUTO" : "MANUAL",
             AppTasks_GetIrrigationStateName(),
             HAL_GPIO_ReadPin(LORA_AUX_PORT, LORA_AUX_PIN) == GPIO_PIN_SET ? "HIGH" : "LOW",
             (unsigned int)rtc_wakeup_flag,
             (unsigned int)lora_wakeup_flag,
             (unsigned int)frame_ready,
             (unsigned int)frame_receive,
             (unsigned long)RtcService_GetWakeIrqCount(),
             (unsigned long)PowerManager_GetLoRaWakeIrqCount());
    DebugConsole_Print(message);
#else
    (void)stage;
#endif
}

/* LoRaTask xử lý LoRa, telemetry và Low power. */
void AppTasks_RunLoRa(void *argument)
{
    DebugConsole_Print("[RTOS] LoRaTask started\r\n");

#if WAKE_DEBUG_LOG_ENABLE
    DebugConsole_Print("[WAKE-TEST] UART2=115200 8N1 | RTC and LoRa AUX wake logging enabled\r\n");
    AppTasks_LogWakeState("TASK START");
#endif

    uint32_t stop_cycle_count = 0;

    for (;;)
    {
        /* Ưu tiên xử lý frame đã nhận đủ. Khi có frame, STM32 không được vào STOP. */
        if (frame_ready)
        {
            DebugConsole_Print("[UART1] Frame received: ");
            DebugConsole_Print(rx_frame_buffer);
            DebugConsole_Print("\r\n");

            LoRaProtocol_ProcessFrame(rx_frame_buffer);
            frame_ready = 0;

            osDelay(1);
            continue;
        }

        /* Chỉ LoRaTask sở hữu UART TX và được phép gửi DATA/ACK/Telemetry. */
        TelemetryManager_Service();

        /* Chỉ chuyển E32 và STM32 sang tiết kiệm năng lượng khi hệ thống rảnh. */
        if (!PowerManager_CanEnterStop())
        {
            osDelay(5);
            continue;
        }

        /* E32 chuyển sang Power-Saving trước khi STM32 vào STOP. */
        DebugConsole_Print("[POWER] System IDLE -> E32 POWER SAVING\r\n");
        LoRaE32_SetPowerSavingMode();
        if (!LoRaE32_WaitReady(100))
        {
            DebugConsole_Print("[ERROR] E32 power-saving mode not ready\r\n");
            continue;
        }

        /* AUX có thể đổi mức khi E32 chuyển mode: reset wake flags atomically. */
        __disable_irq();

        lora_wakeup_flag = 0;
        wake_source = WAKE_NONE;
        __HAL_GPIO_EXTI_CLEAR_IT(LORA_AUX_PIN);
        HAL_NVIC_ClearPendingIRQ(EXTI15_10_IRQn);

        __enable_irq();

        /* AUTO IDLE và FAILED đều cần RTC để tiếp tục cập nhật cảm biến.
         * FAILED vẫn khóa relay - đo định kỳ không tự khởi động tưới lại. */
        if (irr_mode == MODE_AUTO && (irr_state == IRR_IDLE || irr_state == IRR_FAILED))
        {
            if (!RtcService_EnsureAlarmAfterSeconds(RTC_WAKEUP_INTERVAL_SEC))
            {
                DebugConsole_Print("[ERROR] RTC alarm could not be armed; STOP skipped\r\n");
                osDelay(100);
                continue;
            }
        }
        else
        {
            RtcService_CancelAlarm();
        }

        /* STM32 vào STOP và tiếp tục tại đây sau khi RTC hoặc AUX đánh thức. */
        stop_cycle_count++;
#if WAKE_DEBUG_LOG_ENABLE
        char cycle_message[64];
        snprintf(cycle_message, sizeof(cycle_message), "[WAKE-TEST] STOP cycle #%lu\r\n", (unsigned long)stop_cycle_count);
        DebugConsole_Print(cycle_message);
        AppTasks_LogWakeState("BEFORE STOP");
#endif

        DebugConsole_Print("[POWER] Enter STM32 STOP\r\n");

        uint8_t entered_stop = PowerManager_EnterStop();

        AppTasks_LogWakeState(entered_stop ? "AFTER STOP" : "STOP SKIPPED");

        __disable_irq();

        uint8_t woke_by_lora = lora_wakeup_flag;
        uint8_t woke_by_rtc = rtc_wakeup_flag;
        lora_wakeup_flag = 0;
        rtc_wakeup_flag = 0;

        __enable_irq();

	    /* Duy trì và xử lý cả hai sự kiện khi RTC và LoRa cùng xuất hiện. */
	    if (woke_by_lora && woke_by_rtc)
	    {
	        DebugConsole_Print("[WAKE-TEST] RTC and LoRa IRQ pending together; both events will be handled\r\n");
	    }

        /* Nếu hai nguồn wake xảy ra đồng thời, ưu tiên CMD từ LoRa. */
	    if(woke_by_lora)
	    {
	        wake_source = WAKE_LORA;
	    }
	    else if(woke_by_rtc)
	    {
	        wake_source = WAKE_RTC;
	    }
	    else
	    {
	        wake_source = WAKE_NONE;
	        DebugConsole_Print("[WAKE-TEST] Wake source unknown\r\n");
	        continue;
	    }

        if (woke_by_rtc)
        {
            DebugConsole_Print("[WAKE] Source = RTC ALARM\r\n");

            /* Ở FAILED chỉ cập nhật cảm biến; FSM vẫn giữ lockout và không tưới lại. */
            if (irr_mode == MODE_AUTO && (irr_state == IRR_IDLE || irr_state == IRR_FAILED))
            {
                if (irr_state == IRR_FAILED)
                {
                    DebugConsole_Print("[AUTO] FAILED lockout -> sensor refresh only\r\n");
                }
                IrrigationControl_RequestMeasurement();
            }

            if (!woke_by_lora)
            {
                wake_source = WAKE_NONE;
                continue;	// Sleep lại
            }
        }

        if (woke_by_lora)
        {
            DebugConsole_Print("[WAKE] Source = LoRa AUX\r\n");

            /* Chờ UART nhận đủ frame mà E32 vừa báo qua AUX. */
            if (!LoRaE32_WaitForFrame(FRAME_TIMEOUT_MS))
            {
                DebugConsole_Print("[ERROR] LoRa wake but UART1 frame timeout\r\n");
                frame_ready = 0;
                wake_source = WAKE_NONE;
                continue;
            }

            DebugConsole_Print("[UART1] Wake frame received: ");
            DebugConsole_Print(rx_frame_buffer);
            DebugConsole_Print("\r\n");
            LoRaProtocol_ProcessFrame(rx_frame_buffer);
            frame_ready = 0;
            wake_source = WAKE_NONE;
        }

        char msg[80];
        /* Kiểm tra mức stack thấp nhất còn lại của LoRaTask. */
        UBaseType_t watermark = uxTaskGetStackHighWaterMark(NULL);
        snprintf(msg, sizeof(msg), "[LoRaTask STACK] Min free = %lu words\r\n", (unsigned long)watermark);
        DebugConsole_Print(msg);

        osDelay(10);
    }
}

/* SensorTask chờ signal để read */
void AppTasks_RunSensor(void *argument)
{
    DebugConsole_Print("[RTOS] SensorTask started\r\n");

    /* SensorTask block hoàn toàn cho đến khi một task khác yêu cầu snapshot. */
    const uint32_t mask = SENSOR_READ_SIGNAL | IRRIGATION_READ_SIGNAL | TELEMETRY_READ_SIGNAL;

    for (;;)
    {
        uint32_t signals = osThreadFlagsWait(mask, osFlagsWaitAny, osWaitForever);

        if ((signals & osFlagsError) != 0) continue;

        /* Nếu nhiều signal đến cùng lúc thì chỉ đọc toàn bộ sensor một lần. */
        if (signals & mask) SensorManager_ReadAll();

        /* REQ từ LoRaTask: báo DATA có thể được tạo. */
        if (signals & SENSOR_READ_SIGNAL)
        	osThreadFlagsSet(LoRaTaskHandle, SENSOR_READY_SIGNAL);

        /* AUTO FSM: xóa pending và báo IrrigationTask đo đã hoàn tất. */
        if (signals & IRRIGATION_READ_SIGNAL)
        {
            irr_measure_pending = 0;
            osThreadFlagsSet(IrrigationTaskHandle, IRRIGATION_READY_SIGNAL);
        }

        /* Streaming: publish snapshot mới cho TelemetryManager. */
        if (signals & TELEMETRY_READ_SIGNAL)
        {
            telemetry_sensor_pending = 0;
            telemetry_ready = 1;
        }
    }
}

/* IrrigationTask chờ trực tiếp event và cập nhật FSM với độ phân giải 10 ms. */
void AppTasks_RunIrrigation(void *argument)
{
    DebugConsole_Print("[RTOS] IrrigationTask started\r\n");

    for (;;)
    {
        /* Chờ CMD hoặc kết quả đo. Timeout ngắn vẫn cho phép safety timer và
         * AUTO FSM được cập nhật đều, đồng thời xử lý CMD ngay khi được báo. */
        uint32_t flags = osThreadFlagsWait(CONTROL_EXEC_SIGNAL | IRRIGATION_READY_SIGNAL, osFlagsWaitAny, 10);

        if ((flags & osFlagsError) == 0 && (flags & CONTROL_EXEC_SIGNAL))
        {
            uint8_t result = IrrigationControl_ProcessCommand();
            osThreadFlagsSet(LoRaTaskHandle, result ? CONTROL_OK_SIGNAL : CONTROL_ERROR_SIGNAL);
        }

        /* 2. Nhận thông báo SensorTask đã hoàn tất phép đo cho AUTO. */
        if ((flags & osFlagsError) == 0 && (flags & IRRIGATION_READY_SIGNAL))
            DebugConsole_Print("[AUTO] Sensor measurement completed\r\n");

        /* 3. Safety timeout cho MANUAL, sau đó cập nhật AUTO FSM. */
        IrrigationControl_ManualSafetyCheck();
        IrrigationControl_AutoUpdate();

    }
}
