#ifndef RX_LORAE32_COMMON_DATATYPES_H
#define RX_LORAE32_COMMON_DATATYPES_H

#include <stdint.h>
#include <stdbool.h>

/* Private typedef -----------------------------------------------------------*/

typedef enum
{
    /* Logical LoRa states, independent of the STM32 relay GPIO polarity. */
    VALVE_OFF = 0,
    VALVE_ON = 1,
    VALVE_UNKNOWN
} ValveState;

typedef enum
{
    /* Logical LoRa states, independent of the STM32 relay GPIO polarity. */
    PUMP_OFF = 0,
    PUMP_ON = 1,
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
    COMMAND_MODE,
    COMMAND_THRESHOLD
} LoRaCommandType;

typedef struct
{
    LoRaCommandType type;
    uint8_t zone;
    bool irr;
    IrrigationMode mode;
    uint32_t revision;      // Mỗi lệnh ON/OFF van của 1 zone sẽ có số phiên bản tăng dần.
    float startThreshold;
    float stopThreshold;
} LoRaCommand;

// Phản hồi trạng thái lệnh từ LoRaTask sang MQTTTask để publish lên Web. Chứa cả lệnh gốc để Web biết lệnh nào đang được phản hồi.
typedef enum
{
    COMMAND_STATUS_QUEUED,
    COMMAND_STATUS_SENT,
    COMMAND_STATUS_RETRYING,
    COMMAND_STATUS_SUCCESS,
    COMMAND_STATUS_FAILED
} CommandStatusCode;

typedef struct
{
    CommandStatusCode status;
    LoRaCommand command;
    uint32_t seq;
    uint8_t attempt;
} CommandStatus;

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
