/*
 * touch_button.c
 *
 *  Created on: May 19, 2026
 *      Author: Admin
 */

#include "touch_button.h"
#include "main.h"
#include "device_control.h"

#define TTP223_FAN_PORT     GPIOA
#define TTP223_FAN_PIN      GPIO_PIN_6

#define TTP223_LED1_PORT    GPIOA
#define TTP223_LED1_PIN     GPIO_PIN_4

#define TTP223_LED2_PORT    GPIOA
#define TTP223_LED2_PIN     GPIO_PIN_5

/* Lưu trạng thái Touch trước đó để chỉ xử lý khi có thay đổi */
uint8_t lastFanTouchState = 1;
uint8_t lastLed1TouchState = 1;
uint8_t lastLed2TouchState = 1;
/* Debounce Touch */
uint32_t lastFanDebounceTime = 0;
uint32_t lastLed1DebounceTime = 0;
uint32_t lastLed2DebounceTime = 0;
/* Tap Touch Count */
uint32_t lastFanTapTime = 0;
uint8_t fanTapCount = 0;

uint32_t lastLed1TapTime = 0;
uint8_t led1TapCount = 0;

uint32_t lastLed2TapTime = 0;
uint8_t led2TapCount = 0;

void touchButtonUpdate(void)
{
	uint8_t fanTouchState = HAL_GPIO_ReadPin(TTP223_FAN_PORT, TTP223_FAN_PIN);
	uint8_t led1TouchState = HAL_GPIO_ReadPin(TTP223_LED1_PORT, TTP223_LED1_PIN);
	uint8_t led2TouchState = HAL_GPIO_ReadPin(TTP223_LED2_PORT, TTP223_LED2_PIN);

	// FAN
	if(fanTouchState != lastFanTouchState)
	{
		if(HAL_GetTick() - lastFanDebounceTime >= 100) 	// debounce 100ms
		{
			lastFanDebounceTime = HAL_GetTick();
			if(HAL_GetTick() - lastFanTapTime <= 400)	// double tap
			{
				fanTapCount++;
			}
			else
			{
				fanTapCount = 1;	// Reset
			}

			lastFanTapTime = HAL_GetTick();

			if(fanTapCount == 2)
			{
				fanMode = 1;	// Auto
				fanTapCount = 0;
			}
			else if(fanTapCount == 1)
			{
				fanMode = 0;	// Manual
				fanState = !fanState;
			}
			lastFanTouchState = fanTouchState;
		}
	}

	// LED1
	if(led1TouchState != lastLed1TouchState)
	{
		if(HAL_GetTick() - lastLed1DebounceTime >= 100) 	// debounce 100ms
		{
			lastLed1DebounceTime = HAL_GetTick();
			if(HAL_GetTick() - lastLed1TapTime <= 400)	// double tap
			{
				led1TapCount++;
			}
			else
			{
				led1TapCount = 1;	// Reset
			}

			lastLed1TapTime = HAL_GetTick();

			if(led1TapCount == 2)
			{
				led1Mode = 1;	// Auto
				led1TapCount = 0;
			}
			else if(led1TapCount == 1)
			{
				led1Mode = 0;	// Manual
				led1State = !led1State;
			}
			lastLed1TouchState = led1TouchState;
		}
	}

	// LED2
	if(led2TouchState != lastLed2TouchState)
	{
		if(HAL_GetTick() - lastLed2DebounceTime >= 100) 	// debounce 100ms
		{
			lastLed2DebounceTime = HAL_GetTick();
			if(HAL_GetTick() - lastLed2TapTime <= 400)	// double tap
			{
				led2TapCount++;
			}
			else
			{
				led2TapCount = 1;	// Reset
			}

			lastLed2TapTime = HAL_GetTick();

			if(led2TapCount == 2)
			{
				led2Mode = 1;	// Auto
				led2TapCount = 0;
			}
			else if(led2TapCount == 1)
			{
				led2Mode = 0;	// Manual
				led2State = !led2State;
			}
			lastLed2TouchState = led2TouchState;
		}
	}
}
