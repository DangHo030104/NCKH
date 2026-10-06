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

/* LoRaTask xử lý LoRa, telemetry và Low power. */
void AppTasks_RunLoRa(void *argument)
{
    DebugConsole_Print("[RTOS] LoRaTask started\r\n");

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
        LoRaE32_SetPowerSavingMode();
        if (!LoRaE32_WaitReady(100)) continue;

        /* AUX có thể đổi mức khi E32 chuyển mode: reset wake flags atomically. */
        __disable_irq();

        lora_wakeup_flag = 0;
        rtc_wakeup_flag = 0;
        wake_source = WAKE_NONE;
        __HAL_GPIO_EXTI_CLEAR_IT(LORA_AUX_PIN);
        HAL_NVIC_ClearPendingIRQ(EXTI15_10_IRQn);

        __enable_irq();

        /* AUTO IDLE dùng RTC để Wake và đo sensor định kỳ. */
        if (irr_mode == MODE_AUTO && irr_state == IRR_IDLE)
        {
            RtcService_SetAlarmAfterSeconds(RTC_WAKEUP_INTERVAL_SEC);
        }

        /* STM32 vào STOP và tiếp tục tại đây sau khi RTC hoặc AUX đánh thức. */
        PowerManager_EnterStop();

        __disable_irq();

        uint8_t woke_by_lora = lora_wakeup_flag;
        uint8_t woke_by_rtc = rtc_wakeup_flag;
        lora_wakeup_flag = 0;
        rtc_wakeup_flag = 0;

        __enable_irq();

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
	        continue;
	    }

        if (wake_source == WAKE_RTC)
        {
            /* RTC chỉ kích hoạt phép đo mới khi AUTO vẫn đang IDLE. */
            if (irr_mode == MODE_AUTO && irr_state == IRR_IDLE)
            {
                IrrigationControl_RequestMeasurement();
            }

            wake_source = WAKE_NONE;
            continue;	// Sleep lại
        }

        if (wake_source == WAKE_LORA)
        {
            /* Chờ UART nhận đủ frame mà E32 vừa báo qua AUX. */
            if (!LoRaE32_WaitForFrame(FRAME_TIMEOUT_MS))
            {
                frame_ready = 0;
                wake_source = WAKE_NONE;
                continue;
            }
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
