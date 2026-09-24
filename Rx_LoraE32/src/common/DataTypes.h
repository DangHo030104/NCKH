#ifndef RX_LORAE32_COMMON_DATATYPES_H
#define RX_LORAE32_COMMON_DATATYPES_H

#include <stdint.h>
#include <stdbool.h>

/* Private typedef -----------------------------------------------------------*/

typedef struct
{
    uint8_t zone;
    bool irr;
} LoRaCommand;

typedef enum
{
    VALVE_OFF,
    VALVE_ON,
    VALVE_UNKNOWN
} ValveState;

typedef enum
{
    MODE_MANUAL,
    MODE_AUTO,
    MODE_UNKNOWN
} IrrigationMode;

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
    
    IrrigationMode irrigationMode;

    uint8_t batteryPercent;
} SensorData;

typedef struct
{
    bool mqttOnline;
    bool wifiOnline;
} DashboardStatus;

#endif // RX_LORAE32_COMMON_DATATYPES_H
