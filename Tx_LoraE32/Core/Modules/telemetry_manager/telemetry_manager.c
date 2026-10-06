#include "telemetry_manager.h"
#include "app_config.h"
#include "irrigation_control.h"
#include "lora_protocol.h"
#include "system_manager.h"

/* ----- Điều phối live telemetry -----
 * Telemetry được tạo khi:
	- Mode thay đổi.
	- Van hoặc bơm thay đổi.
	- FSM chuyển phase.
	- Hệ thống đang tưới và đã đến chu kỳ gửi định kỳ */

void TelemetryManager_Service(void)
{
    uint32_t now = HAL_GetTick();

    /* 1. Ưu tiên frame đang nhận hoặc đang chờ xử lý.
     * Giữ nguyên telemetry_ready để gửi lại sau khi LoRaTask xử lý xong frame */
    if (frame_receive || frame_ready ||
        (last_lora_rx_tick != 0U && (now - last_lora_rx_tick) < LORA_RX_TO_TX_GUARD_MS))
    {
        return;
    }

    /* 2. Gửi telemetry khi dữ liệu đã sẵn sàng và đã qua TX guard time. */
    if (telemetry_ready && (now - last_lora_tx_tick) >= TELEMETRY_TX_GUARD_MS)
    {
        telemetry_ready = 0;
        LoRaProtocol_SendTelemetry();
        last_telemetry_tick = HAL_GetTick();
        return;
    }

    /* 3. Không tạo yêu cầu mới khi yêu cầu telemetry cũ vẫn đang xử lý. */
    if (telemetry_sensor_pending || telemetry_ready) return;

    /* 4. Tạo telemetry mới khi state thay đổi or khi hệ thống đang tưới theo FSM (Auto)
     * và đã đến chu kỳ streaming tiếp theo. */
    if (telemetry_event_pending ||
        (irr_mode == MODE_AUTO && IrrigationControl_IsActive() &&
         (now - last_telemetry_tick) >= TELEMETRY_INTERVAL_MS))
    {
        telemetry_event_pending = 0;
        telemetry_sensor_pending = 1;
        uint32_t flags = osThreadFlagsSet(SensorTaskHandle, TELEMETRY_READ_SIGNAL);
        if ((flags & osFlagsError) != 0U)
        {
            /* Rollback để sự kiện được thử gửi lại ở vòng sau. */
            telemetry_sensor_pending = 0;
            telemetry_event_pending = 1;
        }
    }
}
