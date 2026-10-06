#include "system_manager.h"
#include "lora_e32.h"
#include "sensor_manager.h"
#include <string.h>

/* ----- Lưu trạng thái hiện tại của toàn hệ thống ----- */

/* Sensor data shared between SensorTask, LoRaTask and IrrigationTask. */
volatile uint16_t temp = 0;
volatile uint16_t humi = 0;
uint16_t soil1_adc = 0;
uint16_t soil2_adc = 0;
volatile float sm1 = 0.0f;
volatile float sm2 = 0.0f;
volatile uint8_t sensor_data_valid = 0;
volatile uint32_t last_sensor_update_tick = 0;
volatile float battery_voltage = 0.0f;
volatile uint8_t battery_percent = 0;

/* UART frame state. The ISR publishes only complete frames to LoRaTask. */
char rx_frame_buffer[128] = {0};
volatile uint8_t frame_receive = 0;
volatile uint8_t frame_ready = 0;
volatile uint32_t rx_frame_drop_count = 0;
volatile uint8_t lora_wakeup_flag = 0;
char control_command[64] = {0};

/* Irrigation state machine and relay owner. */
volatile IrrigationMode irr_mode = MODE_AUTO;
volatile ValveState valve1_state = VALVE_OFF;
volatile ValveState valve2_state = VALVE_OFF;
volatile PumpState pump_state = PUMP_OFF;
volatile IrrigationState irr_state = IRR_IDLE;
volatile RelayOwner relay_owner = RELAY_OWNER_NONE;
volatile WakeSource wake_source = WAKE_NONE;
uint32_t irr_state_start = 0;
volatile uint8_t irr_measure_pending = 0;
uint8_t zone1_cycle = 0;
uint8_t zone2_cycle = 0;
volatile uint8_t irr_failed_zone = 0;
volatile uint8_t rtc_wakeup_flag = 0;

/* Ba flag điều phối telemetry:
 * 1. Irrigation state đổi     -> telemetry_event_pending = 1.
 * 2. LoRaTask yêu cầu đo      -> telemetry_sensor_pending = 1.
 * 3. SensorTask đọc xong      -> telemetry_ready = 1.
 * 4. LoRaTask gửi <T,...>     -> telemetry_ready = 0. */
volatile uint8_t telemetry_event_pending = 0;
volatile uint8_t telemetry_sensor_pending = 0;
volatile uint8_t telemetry_ready = 0;
uint32_t last_telemetry_tick = 0;
uint32_t last_lora_tx_tick = 0;
volatile uint32_t last_lora_rx_tick = 0;

void SystemManager_Init(void)
{
    /* Reset communication and wake-up state before enabling sensor/LoRa modules. */
    frame_receive = 0;
    frame_ready = 0;
    last_lora_rx_tick = 0;
    lora_wakeup_flag = 0;
    rtc_wakeup_flag = 0;
    wake_source = WAKE_NONE;

    SensorManager_Init();
    LoRaE32_Init();
}

void SystemManager_GetSensorSnapshot(SensorSnapshot *snapshot)
{
    if (snapshot == NULL) return;
    snapshot->temperature = temp;
    snapshot->humidity = humi;
    snapshot->soil1 = sm1;
    snapshot->soil2 = sm2;
    snapshot->battery_voltage = battery_voltage;
    snapshot->battery_percent = battery_percent;
    snapshot->valid = sensor_data_valid;
    snapshot->updated_tick = last_sensor_update_tick;
}
