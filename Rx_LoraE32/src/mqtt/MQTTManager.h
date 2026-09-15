#ifndef RX_LORAE32_MQTT_MQTTMANAGER_H
#define RX_LORAE32_MQTT_MQTTMANAGER_H

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

void MQTTManager_Begin(QueueHandle_t commands, QueueHandle_t mqttData);
void MQTTManager_Run(void *pvParameters);

#endif // RX_LORAE32_MQTT_MQTTMANAGER_H
