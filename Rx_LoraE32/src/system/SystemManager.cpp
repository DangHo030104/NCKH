#include "SystemManager.h"
#include "../rtos/TaskManager.h"
#include "../lora/LoRaManager.h"
#include "../mqtt/MQTTManager.h"
#include "../wifi/WiFiManager.h"
#include "../display/DisplayManager.h"
#include <Arduino.h>
#include <time.h>

void SystemManager_SyncTime(void)
{
    // Start/restart background NTP synchronization on Wi-Fi connection.
    // Vietnam uses UTC+7 with no daylight saving time.
    configTime(7 * 3600, 0, "pool.ntp.org", "time.google.com");
}

void SystemManager_Begin(void)
{
    Serial.begin(115200);
    TaskManager_Queues queues = {};
    if (!TaskManager_CreateQueues(&queues))
    {
        while (1) { delay(1000); }
    }

    DisplayManager_Begin(queues.displayData);
    LoRaManager_Begin(queues.commands, queues.mqttData, queues.displayData);
    WiFiManager_Begin();
    MQTTManager_Begin(queues.commands, queues.mqttData);
    randomSeed(millis());

    if (!TaskManager_StartTasks())
    {
        Serial.println("[ERROR] Task creation failed -> restarting");
        ESP.restart();
        while (1) { delay(1000); }
    }
}
