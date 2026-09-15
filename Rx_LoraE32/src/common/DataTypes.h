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

typedef struct
{
    float temperature;
    float humidity;
    float soil1;
    float soil2;

    uint32_t seq;
    uint32_t receivedAt;

    // bool loraConnected;
    // bool mqttConnected;
    // bool wifiConnected;

} SensorData;

typedef enum
{
    COMMAND_DISPLAY_NONE,
    COMMAND_DISPLAY_WAIT,
    COMMAND_DISPLAY_ACK,
    COMMAND_DISPLAY_FAIL
} CommandDisplayState;

typedef struct
{
    bool mqttOnline;
    bool wifiOnline;
    uint8_t zone;
    bool irrigationOn;
    uint8_t attempt;
    CommandDisplayState command;
} DashboardStatus;

#endif // RX_LORAE32_COMMON_DATATYPES_H
