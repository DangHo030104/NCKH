/*
 * DHT11.h
 *
 *  Created on: May 10, 2026
 *      Author: Admin
 */

#ifndef __DHT11_H__
#define __DHT11_H__

#include "stm32f1xx_hal.h"

typedef struct {
	TIM_HandleTypeDef *DHT_TIM;
	uint16_t DHT_PIN;
	GPIO_TypeDef *DHT_PORT;
    uint16_t Temperature;
    uint16_t Humidity;
} DHT11_DataTypedef;

void DHT11_Init(DHT11_DataTypedef *DHT, TIM_HandleTypeDef *Timer, GPIO_TypeDef *DHT_Port, uint16_t DHT_Pin);
uint8_t DHT11_Read_Data(DHT11_DataTypedef *DHT);

#endif
/* __DHT11_H__ */
