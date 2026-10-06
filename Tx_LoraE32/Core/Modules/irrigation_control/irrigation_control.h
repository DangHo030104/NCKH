#ifndef IRRIGATION_CONTROL_H
#define IRRIGATION_CONTROL_H

#include <stdint.h>

uint8_t IrrigationControl_RequestMeasurement(void);
void IrrigationControl_AutoUpdate(void);
uint8_t IrrigationControl_ProcessCommand(void);
void IrrigationControl_ManualSafetyCheck(void);
uint8_t IrrigationControl_IsActive(void);
uint8_t IrrigationControl_GetActiveZone(void);
uint8_t IrrigationControl_GetPhase(void);
uint8_t IrrigationControl_GetCycle(void);

#endif
