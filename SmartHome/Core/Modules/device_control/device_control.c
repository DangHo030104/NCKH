/*
 * device_control.c
 *
 *  Created on: May 19, 2026
 *      Author: Admin
 */

#include "device_control.h"
#include "sensor_manager.h"

#define RELAY1_FAN_PORT     GPIOB
#define RELAY1_FAN_PIN      GPIO_PIN_0

#define RELAY2_LED1_PORT    GPIOB
#define RELAY2_LED1_PIN     GPIO_PIN_1

#define RELAY3_LED2_PORT    GPIOB
#define RELAY3_LED2_PIN     GPIO_PIN_10

uint16_t dark_Threshold = 2500;
uint16_t bright_Threshold = 1500;

uint16_t tempOnFan = 30;
uint16_t tempOffFan = 28;

uint8_t led1State = 0;
uint8_t led2State = 0;
uint8_t fanState = 0;

/* MODE: 0: MANUAL, 1: AUTO */
uint8_t led1Mode = 1;
uint8_t led2Mode = 1;
uint8_t fanMode = 1;

void deviceControlUpdate(void)
{
	// LED1: sân vườn
    if(led1Mode)
    {
        if(ldrValue >= dark_Threshold)
        {
            led1State = 1;
        }
        else if(ldrValue <= bright_Threshold)
        {
            led1State = 0;
        }
    }

    HAL_GPIO_WritePin(RELAY2_LED1_PORT, RELAY2_LED1_PIN,
                      led1State ? GPIO_PIN_RESET : GPIO_PIN_SET);

    // LED2: trong nhà
    if(led2Mode)
    {
        if(ldrValue >= dark_Threshold && presenceState)
        {
            led2State = 1;
        }
        else if(ldrValue <= bright_Threshold || !presenceState)
        {
            led2State = 0;
        }
    }

    HAL_GPIO_WritePin(RELAY3_LED2_PORT, RELAY3_LED2_PIN,
                      led2State ? GPIO_PIN_RESET : GPIO_PIN_SET);

    // FAN
    if(fanMode)
    {
        if(temp >= tempOnFan && presenceState)
        {
            fanState = 1;
        }
        else if(temp <= tempOffFan || !presenceState)
        {
            fanState = 0;
        }
    }

    HAL_GPIO_WritePin(RELAY1_FAN_PORT, RELAY1_FAN_PIN,
                      fanState ? GPIO_PIN_RESET : GPIO_PIN_SET);
}
