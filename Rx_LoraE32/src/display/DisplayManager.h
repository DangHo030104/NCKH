#ifndef RX_LORAE32_DISPLAY_DISPLAYMANAGER_H
#define RX_LORAE32_DISPLAY_DISPLAYMANAGER_H

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

void DisplayManager_Begin(QueueHandle_t displayData);
void DisplayManager_Run(void *pvParameters);

#endif // RX_LORAE32_DISPLAY_DISPLAYMANAGER_H
