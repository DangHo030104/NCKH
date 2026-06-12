/*
 * pc_serial_com.c
 *
 *  Created on: May 19, 2026
 *      Author: Admin
 */

#include "pc_serial_com.h"
#include "sensor_manager.h"
#include "device_control.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static uint16_t ReadNumberFromPC(UART_HandleTypeDef *huart);

void PC_Serial_ProcessCommand(uint8_t cmd, UART_HandleTypeDef *huart)
{
    char buffer[256];
    switch(cmd)
    {
    	case '0':
    	    sprintf(buffer,
    	        "Temp: %d*C - Humi: %d%% | LDR:%d | RADAR: %s - Moving: %s - Static: %s\r\n"
    	        "LED1: %s (%s) | LED2: %s (%s) | FAN: %s (%s)\r\n\r\n",
    	        temp, humi, ldrValue,
    			presenceState ? "DETECTED" : "NO DETECT", movingTarget ? "YES" : "NO", staticTarget ? "YES" : "NO",
    	        led1State ? "ON" : "OFF", led1Mode ? "AUTO" : "MANUAL",
    	        led2State ? "ON" : "OFF", led2Mode ? "AUTO" : "MANUAL",
    	        fanState  ? "ON" : "OFF", fanMode  ? "AUTO" : "MANUAL"
    	    );

    	    HAL_UART_Transmit(huart, (uint8_t*)buffer, strlen(buffer), 100);
    		break;

        case '1':
            sprintf(buffer, "LED1: %s (%s)\r\n\r\n", led1State ? "ON":"OFF", led1Mode ? "AUTO":"MANUAL");
            HAL_UART_Transmit(huart, (uint8_t*)buffer, strlen(buffer), 100);
            break;

        case '2':
            sprintf(buffer, "LED2: %s (%s)\r\n\r\n", led2State ? "ON":"OFF", led2Mode ? "AUTO":"MANUAL");
            HAL_UART_Transmit(huart, (uint8_t*)buffer, strlen(buffer), 100);
            break;

        case '3':
            sprintf(buffer, "FAN: %s (%s)\r\n\r\n", fanState ? "ON":"OFF", fanMode ? "AUTO":"MANUAL");
            HAL_UART_Transmit(huart, (uint8_t*)buffer, strlen(buffer), 100);
            break;

        case '4':
            sprintf(buffer, "Temperature: %d*C - Humidity: %d%%\r\n\r\n", temp, humi);
            HAL_UART_Transmit(huart, (uint8_t*)buffer, strlen(buffer), 100);
            break;

        case '5':
            sprintf(buffer, "LDR: %d\r\n\r\n", ldrValue);
            HAL_UART_Transmit(huart, (uint8_t*)buffer, strlen(buffer), 100);
            break;

        case '6':
            sprintf(buffer, "Radar presence: %s\r\n"
            				"Moving Target: %s\r\n"
            				"Stationary Target: %s\r\n\r\n"
            				, presenceState ? "DETECTED":"NO DETECT"
            				, movingTarget ? "YES" : "NO"
            				, staticTarget ? "YES" : "NO");
            HAL_UART_Transmit(huart, (uint8_t*)buffer, strlen(buffer), 100);
            break;

        case '7':
            led1Mode = 0; // Manual
            led1State = !led1State;
            sprintf(buffer, "LED1: %s (%s)\r\n\r\n", led1State ? "ON":"OFF", led1Mode ? "AUTO" : "MANUAL");
            HAL_UART_Transmit(huart, (uint8_t*)buffer, strlen(buffer), 100);
            break;

        case '8':
            led2Mode = 0;
            led2State = !led2State;
            sprintf(buffer, "LED2: %s (%s)\r\n\r\n", led2State ? "ON":"OFF", led2Mode ? "AUTO" : "MANUAL");
            HAL_UART_Transmit(huart, (uint8_t*)buffer, strlen(buffer), 100);
            break;

        case '9':
            fanMode = 0;
            fanState = !fanState;
            sprintf(buffer, "FAN: %s (%s)\r\n\r\n", fanState ? "ON":"OFF", fanMode ? "AUTO" : "MANUAL");
            HAL_UART_Transmit(huart, (uint8_t*)buffer, strlen(buffer), 100);
            break;

        case 'a':
        case 'A':
        	sprintf(buffer, "TEMP_ON_FAN: %d - TEMP_OFF_FAN: %d\r\n\r\n", tempOnFan, tempOffFan);
        	HAL_UART_Transmit(huart, (uint8_t*)buffer, strlen(buffer), 100);
        	break;

        case 'b':
        case 'B':
        	sprintf(buffer, "DARK_THRESHOLD: %d - BRIGHT_THRESHOLD: %d\r\n\r\n", dark_Threshold, bright_Threshold);
        	HAL_UART_Transmit(huart, (uint8_t*)buffer, strlen(buffer), 100);
        	break;

        case 'c':
        case 'C':
            sprintf(buffer, "Enter new TEMP_ON_FAN (*C): ");
            HAL_UART_Transmit(huart, (uint8_t*)buffer, strlen(buffer), 100);
            tempOnFan = ReadNumberFromPC(huart);
            sprintf(buffer, "\r\nTEMP_ON_FAN set to %d*C\r\n\r\n", tempOnFan);
            HAL_UART_Transmit(huart, (uint8_t*)buffer, strlen(buffer), 100);
            break;

        case 'd':
        case 'D':
			sprintf(buffer, "Enter new TEMP_OFF_FAN (*C): ");
			HAL_UART_Transmit(huart, (uint8_t*) buffer, strlen(buffer), 100);
			tempOffFan = ReadNumberFromPC(huart);
			sprintf(buffer, "\r\nTEMP_OFF_FAN set to %d*C\r\n\r\n", tempOffFan);
			HAL_UART_Transmit(huart, (uint8_t*) buffer, strlen(buffer), 100);
			break;

        case 'e':
        case 'E':
            sprintf(buffer, "Enter new DARK_THRESHOLD (ADC): ");
            HAL_UART_Transmit(huart, (uint8_t*)buffer, strlen(buffer), 100);
            dark_Threshold = ReadNumberFromPC(huart);
            sprintf(buffer, "\r\nDARK_THRESHOLD set to %d\r\n\r\n", dark_Threshold);
            HAL_UART_Transmit(huart, (uint8_t*)buffer, strlen(buffer), 100);
            break;

        case 'f':
        case 'F':
            sprintf(buffer, "Enter new BRIGHT_THRESHOLD (ADC): ");
            HAL_UART_Transmit(huart, (uint8_t*)buffer, strlen(buffer), 100);
            bright_Threshold = ReadNumberFromPC(huart);
            sprintf(buffer, "\r\nBRIGHT_THRESHOLD set to %d\r\n\r\n", bright_Threshold);
            HAL_UART_Transmit(huart, (uint8_t*)buffer, strlen(buffer), 100);
            break;

        case 'g':
        case 'G':
        	led1Mode = !led1Mode;
        	sprintf(buffer, "LED1 MODE: %s\r\n\r\n", led1Mode ? "AUTO" : "MANUAL");
        	HAL_UART_Transmit(huart, (uint8_t*)buffer, strlen(buffer), 100);
        	break;

        case 'h':
        case 'H':
        	led2Mode = !led2Mode;
        	sprintf(buffer, "LED2 MODE: %s\r\n\r\n", led2Mode ? "AUTO" : "MANUAL");
        	HAL_UART_Transmit(huart, (uint8_t*)buffer, strlen(buffer), 100);
        	break;

        case 'i':
        case 'I':
        	fanMode = !fanMode;
        	sprintf(buffer, "FAN MODE: %s\r\n\r\n", fanMode ? "AUTO" : "MANUAL");
        	HAL_UART_Transmit(huart, (uint8_t*)buffer, strlen(buffer), 100);
        	break;

        default:	// MENU
            HAL_UART_Transmit(huart, (uint8_t*)"================ Available commands ================\r\n", strlen("================ Available commands ================\r\n"), 100);
            HAL_UART_Transmit(huart, (uint8_t*)"'0': All system states\r\n", strlen("'0': All system states\r\n"), 100);
            HAL_UART_Transmit(huart, (uint8_t*)"'1': Status and mode of LED1\r\n", strlen("'1': Status and mode of LED1\r\n"), 100);
            HAL_UART_Transmit(huart, (uint8_t*)"'2': Status and mode of LED2\r\n", strlen("'2': Status and mode of LED2\r\n"), 100);
            HAL_UART_Transmit(huart, (uint8_t*)"'3': Status and mode of FAN\r\n", strlen("'3': Status and mode of FAN\r\n"), 100);
            HAL_UART_Transmit(huart, (uint8_t*)"'4': Temperature and Humidity of the DHT11 Sensor\r\n", strlen("'4': Temperature and Humidity of the DHT11 Sensor\r\n"), 100);
            HAL_UART_Transmit(huart, (uint8_t*)"'5': ADC value of the light sensor\r\n", strlen("'5': ADC value of the light sensor\r\n"), 100);
            HAL_UART_Transmit(huart, (uint8_t*)"'6': Status of human presence sensor\r\n", strlen("'6': Status of human presence sensor\r\n"), 100);
            HAL_UART_Transmit(huart, (uint8_t*)"'7': Toogle LED1 in manual mode\r\n", strlen("'7': Toogle LED1 in manual mode\r\n"), 100);
            HAL_UART_Transmit(huart, (uint8_t*)"'8': Toogle LED2 in manual mode\r\n", strlen("'8': Toogle LED2 in manual mode\r\n"), 100);
            HAL_UART_Transmit(huart, (uint8_t*)"'9': Toogle FAN in manual mode\r\n", strlen("'9': Toogle FAN in manual mode\r\n"), 100);
            HAL_UART_Transmit(huart, (uint8_t*)"'a' or 'A': Get TEMP_ON_FAN and TEMP_OFF_FAN\r\n", strlen("'a' or 'A': Get TEMP_ON_FAN and TEMP_OFF_FAN\r\n"), 100);
            HAL_UART_Transmit(huart, (uint8_t*)"'b' or 'B': Get DARK_THRESHOLD and BRIGHT_THRESHOLD\r\n", strlen("'b' or 'B': Get DARK_THRESHOLD and BRIGHT_THRESHOLD\r\n"), 100);
            HAL_UART_Transmit(huart, (uint8_t*)"'c' or 'C': Set TEMP_ON_FAN\r\n", strlen("'c' or 'C': Set TEMP_ON_FAN\r\n"), 100);
            HAL_UART_Transmit(huart, (uint8_t*)"'d' or 'D': Set TEMP_OFF_FAN\r\n", strlen("'d' or 'D': Set TEMP_OFF_FAN\r\n"), 100);
            HAL_UART_Transmit(huart, (uint8_t*)"'e' or 'E': Set DARK_THRESHOLD\r\n", strlen("'e' or 'E': Set DARK_THRESHOLD\r\n"), 100);
            HAL_UART_Transmit(huart, (uint8_t*)"'f' or 'F': Set BRIGHT_THRESHOLD\r\n", strlen("'f' or 'F': Set BRIGHT_THRESHOLD\r\n"), 100);
            HAL_UART_Transmit(huart, (uint8_t*)"'g' or 'G': Toggle LED1 AUTO/MANUAL mode\r\n", strlen("'g' or 'G': Toggle LED1 AUTO/MANUAL mode\r\n"), 100);
            HAL_UART_Transmit(huart, (uint8_t*)"'h' or 'H': Toggle LED2 AUTO/MANUAL mode\r\n", strlen("'h' or 'H': Toggle LED2 AUTO/MANUAL mode\r\n"), 100);
            HAL_UART_Transmit(huart, (uint8_t*)"'i' or 'I': Toggle FAN AUTO/MANUAL mode\r\n\r\n", strlen("'i' or 'I': Toggle FAN AUTO/MANUAL mode\r\n\r\n"), 100);

    }
}

static uint16_t ReadNumberFromPC(UART_HandleTypeDef *huart)
{
    char str[6] = {0};	// Buff chưa 5 số + '\0'
    int idx = 0;		// Index trong buff
    uint8_t c = 0;		// Byte nhận từ UART

    while(1)
    {
        HAL_UART_Receive(huart, &c, 1, 100);	// Chờ nhận 1 byte
        HAL_UART_Transmit(huart, &c, 1, 100);	// Echo ký tự vừa nhập về PC
        if(c == '\r' || c == '\n') break;
        if(idx < 5) str[idx++] = c;
    }
    return atoi(str);	// Chuyển string -> int
}

