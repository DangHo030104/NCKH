#include "relay_driver.h"
#include "app_config.h"
#include "debug_console.h"
#include "system_manager.h"
#include "cmsis_os2.h"

/* ----- Trực tiếp điều khiển Relay ----- */

void RelayDriver_StartZone1(void)
{
    DebugConsole_Print("[IRR] Start Zone1\r\n");

    /* Tắt van Zone 2 trước khi mở van Zone 1. */
    HAL_GPIO_WritePin(RELAY2_PORT, RELAY2_PIN, VALVE_RELAY_OFF);
    HAL_GPIO_WritePin(RELAY1_PORT, RELAY1_PIN, VALVE_RELAY_ON);
    valve1_state = VALVE_ON;
    valve2_state = VALVE_OFF;

    /* Chờ van mở ổn định rồi mới bật bơm. */
    osDelay(500);

    HAL_GPIO_WritePin(RELAY3_PORT, RELAY3_PIN, PUMP_RELAY_ON);
    pump_state = PUMP_ON;

    HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET);	// Debug

    // Đánh dấu cần gửi telemetry.
    telemetry_event_pending = 1;
}

void RelayDriver_StartZone2(void)
{
    DebugConsole_Print("[IRR] Start Zone2\r\n");

    /* Tắt van Zone 1 trước khi mở van Zone 2. */
    HAL_GPIO_WritePin(RELAY1_PORT, RELAY1_PIN, VALVE_RELAY_OFF);
    HAL_GPIO_WritePin(RELAY2_PORT, RELAY2_PIN, VALVE_RELAY_ON);
    valve1_state = VALVE_OFF;
    valve2_state = VALVE_ON;

    /* Chờ van mở ổn định rồi mới bật bơm. */
    osDelay(500);

    HAL_GPIO_WritePin(RELAY3_PORT, RELAY3_PIN, PUMP_RELAY_ON);
    pump_state = PUMP_ON;

    HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET);

    telemetry_event_pending = 1;
}

void RelayDriver_StopAll(void)
{
    DebugConsole_Print("[IRR] Stop all irrigation\r\n");

    /* Tắt bơm trước, sau đó mới đóng cả hai van. */
    HAL_GPIO_WritePin(RELAY3_PORT, RELAY3_PIN, PUMP_RELAY_OFF);
    pump_state = PUMP_OFF;

    osDelay(500);

    HAL_GPIO_WritePin(RELAY1_PORT, RELAY1_PIN, VALVE_RELAY_OFF);
    HAL_GPIO_WritePin(RELAY2_PORT, RELAY2_PIN, VALVE_RELAY_OFF);
    valve1_state = VALVE_OFF;
    valve2_state = VALVE_OFF;

    HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET);

    /* Đồng bộ trạng thái logic với relay thực tế để DATA gửi lên ESP32/Web
     * không còn báo van ON sau khi relay đã OFF. */
}

void RelayDriver_StopCurrent(RelayOwner owner)
{
    /* Không cho AUTO can thiệp khi MANUAL đang sở hữu relay và ngược lại. */
    if (owner != relay_owner)
    {
        DebugConsole_Print("[RELAY] Stop rejected\r\n");
        return;
    }
    DebugConsole_Print("[IRR] Stop current zone\r\n");
    RelayDriver_StopAll();
}
