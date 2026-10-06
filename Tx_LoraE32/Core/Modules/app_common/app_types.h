#ifndef APP_TYPES_H
#define APP_TYPES_H

/* ----- Định nghĩa các kiểu trạng thái của hệ thống -----*/

#include <stdint.h>

/* ===== STATE MACHINE ===== */
typedef enum {
	MODE_MANUAL = 0,
	MODE_AUTO
} IrrigationMode;

typedef enum {
	VALVE_OFF = 0,
	VALVE_ON
} ValveState;

typedef enum {
	PUMP_OFF = 0,
	PUMP_ON
} PumpState;

// FSM tưới AUTO
typedef enum
{
    IRR_IDLE = 0,

    IRR_ZONE1_WATERING,
    IRR_ZONE1_SOAK,
    IRR_ZONE1_MEASURE,

    IRR_ZONE2_WATERING,
    IRR_ZONE2_SOAK,
    IRR_ZONE2_MEASURE,

    /* AUTO lockout after a zone reaches MAX_IRRIGATION_CYCLE. */
    IRR_FAILED
} IrrigationState;

// Ngăn AUTO và MANUAL cùng điều khiển relay.
typedef enum
{
    RELAY_OWNER_NONE = 0,
    RELAY_OWNER_MANUAL,
    RELAY_OWNER_AUTO
} RelayOwner;

typedef enum {
	WAKE_NONE = 0,
	WAKE_RTC,
	WAKE_LORA
} WakeSource;

typedef struct
{
    uint16_t temperature;
    uint16_t humidity;
    float soil1;
    float soil2;
    float battery_voltage;
    uint8_t battery_percent;
    uint8_t valid;
    uint32_t updated_tick;
} SensorSnapshot;

#endif
