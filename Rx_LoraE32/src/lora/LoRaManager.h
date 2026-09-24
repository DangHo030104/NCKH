#ifndef RX_LORAE32_LORA_LORAMANAGER_H
#define RX_LORAE32_LORA_LORAMANAGER_H

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

void LoRaManager_Begin(QueueHandle_t commands, QueueHandle_t mqttData, QueueHandle_t displayData);
void LoRaManager_Run(void *pvParameters);

#endif // RX_LORAE32_LORA_LORAMANAGER_H
