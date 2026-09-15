#ifndef RX_LORAE32_RTOS_TASKMANAGER_H
#define RX_LORAE32_RTOS_TASKMANAGER_H

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

typedef struct
{
    QueueHandle_t commands;
    QueueHandle_t mqttData;
    QueueHandle_t displayData;
} TaskManager_Queues;

bool TaskManager_CreateQueues(TaskManager_Queues *queues);
bool TaskManager_StartTasks(void);

#endif // RX_LORAE32_RTOS_TASKMANAGER_H
