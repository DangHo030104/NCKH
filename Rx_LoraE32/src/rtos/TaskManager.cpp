#include "TaskManager.h"
#include "../common/DataTypes.h"
#include "../lora/LoRaManager.h"
#include "../mqtt/MQTTManager.h"
#include "../display/DisplayManager.h"
#include <Arduino.h>

/* RTOS TASKS */
static TaskHandle_t loraTaskHandle;
static TaskHandle_t mqttTaskHandle;
static TaskHandle_t displayTaskHandle;

/* RTOS QUEUES */
static QueueHandle_t commandQueue;
static QueueHandle_t mqttDataQueue;
static QueueHandle_t displayQueue;


bool TaskManager_CreateQueues(TaskManager_Queues *queues)
{
    /* Create Queues */
    commandQueue = xQueueCreate(5, sizeof(LoRaCommand));
    mqttDataQueue = xQueueCreate(5, sizeof(SensorData));
    displayQueue = xQueueCreate(5, sizeof(SensorData));

    if (commandQueue == NULL || mqttDataQueue == NULL || displayQueue == NULL)
    {
        Serial.println("[ERROR] Queue creation failed");
        return false;
    }

    Serial.println("[RTOS] Queues created!");

    queues->commands = commandQueue;
    queues->mqttData = mqttDataQueue;
    queues->displayData = displayQueue;
    return true;
}

bool TaskManager_StartTasks(void)
{
    /* Create RTOS Tasks */
    if (xTaskCreate(
        LoRaManager_Run,                            // Task function
        "LoRaTask",                                 // Task name
        4096,                                       // Stack size (bytes)
        NULL,                                       // Task parameters
        2,                                          // Task priority
        &loraTaskHandle) != pdPASS) return false;   // Task handle

    if (xTaskCreate(
        MQTTManager_Run,
        "MQTTTask",
        4096,
        NULL,
        1,
        &mqttTaskHandle) != pdPASS) return false;

    if (xTaskCreate(
        DisplayManager_Run,
        "DisplayTask",
        4096,
        NULL,
        1,
        &displayTaskHandle) != pdPASS) return false;

    Serial.println("[RTOS] Tasks created!");
    return true;
}
