#ifndef RX_LORAE32_COMMON_DATATYPES_H
#define RX_LORAE32_COMMON_DATATYPES_H

#include <stdint.h>
#include <stdbool.h>

/* Private typedef -----------------------------------------------------------*/

typedef enum
{
    VALVE_OFF,
    VALVE_ON,
    VALVE_UNKNOWN
} ValveState;

typedef enum
{
    PUMP_OFF,
    PUMP_ON,
    PUMP_UNKNOWN
} PumpState;

typedef enum
{
    MODE_MANUAL,
    MODE_AUTO,
    MODE_UNKNOWN
} IrrigationMode;

typedef enum
{
    IRRIGATION_PHASE_IDLE,
    IRRIGATION_PHASE_WATERING,
    IRRIGATION_PHASE_SOAK,
    IRRIGATION_PHASE_MEASURING,
    IRRIGATION_PHASE_FAILED,
    IRRIGATION_PHASE_UNKNOWN
} IrrigationPhase;

typedef enum
{
    COMMAND_IRRIGATION,
    COMMAND_MODE
} LoRaCommandType;

typedef struct
{
    LoRaCommandType type;
    uint8_t zone;
    bool irr;
    IrrigationMode mode;
} LoRaCommand;

typedef struct
{
    float temperature;
    float humidity;

    float soil1;
    float soil2;

    uint32_t seq;
    uint32_t receivedAt;

    // Reported by STM32 in DATA, never inferred from a command ACK.
    ValveState valve1;
    ValveState valve2;
    PumpState pump;
    
    IrrigationMode irrigationMode;
    uint8_t batteryPercent;
    uint8_t activeZone;
    IrrigationPhase irrigationPhase;
    uint8_t irrigationCycle;
    bool streaming;
} SensorData;

typedef struct
{
    bool mqttOnline;
    bool wifiOnline;
} DashboardStatus;

#endif // RX_LORAE32_COMMON_DATATYPES_H
