#include <Arduino.h>
#include "system/SystemManager.h"

void setup(void)
{
    SystemManager_Begin();
}

void loop(void)
{
    /* Arduino loopTask Block và nhường CPU 1s */
    vTaskDelay(pdMS_TO_TICKS(1000));
}
