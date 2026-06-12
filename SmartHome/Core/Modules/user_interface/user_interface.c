/*
 * user_interface.c
 *
 *  Created on: May 20, 2026
 *      Author: Admin
 */

#include "user_interface.h"

#include "main.h"
#include "sensor_manager.h"
#include "device_control.h"

#include <stdio.h>
#include "ssd1306.h"
#include "ssd1306_fonts.h"

static uint32_t last_oled = 0;

void userInterfaceInit(void) {
	ssd1306_Init();

	ssd1306_Fill(Black);	// Clear màn hình

	ssd1306_SetCursor(0, 0);
	ssd1306_WriteString("SMART HOME", Font_11x18, White);// Vẽ text vào RAM buffer

	ssd1306_SetCursor(0, 24);
	ssd1306_WriteString("Welcome to", Font_7x10, White);

	ssd1306_SetCursor(0, 35);
	ssd1306_WriteString("BLACK HOME!", Font_7x10, White);

	ssd1306_UpdateScreen();

	HAL_Delay(2000);
}

void userInterfaceUpdate(void) {
	// Update OLED 500ms
	if (HAL_GetTick() - last_oled >= 500) {
		last_oled = HAL_GetTick();
		char buffer[64];

		ssd1306_Fill(Black);	// Clear màn hình

		sprintf(buffer, "T: %d*C - H: %d%%", temp, humi);
		ssd1306_SetCursor(0, 0);
		ssd1306_WriteString(buffer, Font_7x10, White);

		sprintf(buffer, "LDR: %d", ldrValue);
		ssd1306_SetCursor(0, 12);
		ssd1306_WriteString(buffer, Font_7x10, White);

		sprintf(buffer, "RADAR: %s", presenceState ? "DETECTED" : "NO DETECT");
		ssd1306_SetCursor(0, 24);
		ssd1306_WriteString(buffer, Font_7x10, White);

		sprintf(buffer, "---------------------");
		ssd1306_SetCursor(0, 36);
		ssd1306_WriteString(buffer, Font_6x8, White);

		sprintf(buffer, "L1:%s(%s) - L2:%s(%s)", led1State ? "ON" : "OFF",
				led1Mode ? "A" : "M", led2State ? "ON" : "OFF",
				led2Mode ? "A" : "M");
		ssd1306_SetCursor(0, 46);
		ssd1306_WriteString(buffer, Font_6x8, White);

		sprintf(buffer, "FAN:%s(%s)", fanState ? "ON" : "OFF",
				fanMode ? "A" : "M");
		ssd1306_SetCursor(0, 56);
		ssd1306_WriteString(buffer, Font_6x8, White);

		ssd1306_UpdateScreen();
	}
}
