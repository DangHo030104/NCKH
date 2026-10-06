#ifndef SYSTEM_MANAGER_H
#define SYSTEM_MANAGER_H

#include "app_types.h"
#include "cmsis_os2.h"
#include "main.h"

extern ADC_HandleTypeDef hadc1;
extern RTC_HandleTypeDef hrtc;
extern TIM_HandleTypeDef htim1;
extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;

extern osThreadId_t LoRaTaskHandle;
extern osThreadId_t SensorTaskHandle;
extern osThreadId_t IrrigationTaskHandle;

extern volatile uint16_t temp;
extern volatile uint16_t humi;
extern uint16_t soil1_adc;
extern uint16_t soil2_adc;
extern volatile float sm1;
extern volatile float sm2;
extern volatile uint8_t sensor_data_valid;
extern volatile uint32_t last_sensor_update_tick;
extern volatile float battery_voltage;
extern volatile uint8_t battery_percent;

extern char rx_frame_buffer[128];
extern volatile uint8_t frame_receive;
extern volatile uint8_t frame_ready;
extern volatile uint32_t rx_frame_drop_count;
extern volatile uint8_t lora_wakeup_flag;
extern char control_command[64];

extern volatile IrrigationMode irr_mode;
extern volatile ValveState valve1_state;
extern volatile ValveState valve2_state;
extern volatile PumpState pump_state;
extern volatile IrrigationState irr_state;
extern volatile RelayOwner relay_owner;
extern volatile WakeSource wake_source;
extern uint32_t irr_state_start;
extern volatile uint8_t irr_measure_pending;
extern uint8_t zone1_cycle;
extern uint8_t zone2_cycle;
extern volatile uint8_t irr_failed_zone;
extern volatile uint8_t rtc_wakeup_flag;

extern volatile uint8_t telemetry_event_pending;
extern volatile uint8_t telemetry_sensor_pending;
extern volatile uint8_t telemetry_ready;
extern uint32_t last_telemetry_tick;
extern uint32_t last_lora_tx_tick;
extern volatile uint32_t last_lora_rx_tick;

void SystemManager_Init(void);
void SystemManager_GetSensorSnapshot(SensorSnapshot *snapshot);

#endif
