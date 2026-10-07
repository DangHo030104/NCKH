#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include <stdint.h>

uint8_t PowerManager_CanEnterStop(void);
uint8_t PowerManager_EnterStop(void);
uint32_t PowerManager_GetLoRaWakeIrqCount(void);

#endif
