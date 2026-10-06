#ifndef LORA_E32_H
#define LORA_E32_H

#include <stdint.h>

void LoRaE32_Init(void);
uint8_t LoRaE32_WaitReady(uint32_t timeout_ms);
uint8_t LoRaE32_WaitForFrame(uint32_t timeout_ms);
void LoRaE32_SetNormalMode(void);
void LoRaE32_SetPowerSavingMode(void);

#endif
