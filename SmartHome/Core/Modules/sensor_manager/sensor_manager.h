/*
 * sensor_manager.h
 *
 *  Created on: May 19, 2026
 *      Author: Admin
 */

#ifndef _SENSOR_MANAGER_H_
#define _SENSOR_MANAGER_H_

#include "main.h"

void sensorManagerInit(void);
void sensorManagerUpdate(void);

void Sensor_Radar_UART_Callback(uint8_t byte);

extern uint16_t temp;
extern uint16_t humi;
extern uint16_t ldrValue;

extern uint8_t presenceState;
extern uint8_t movingTarget;
extern uint8_t staticTarget;

#endif /* _SENSOR_MANAGER_H_ */
