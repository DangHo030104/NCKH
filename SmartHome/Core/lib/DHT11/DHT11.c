/*
 * DHT11.c
 *
 *  Created on: May 10, 2026
 *      Author: Admin
 */

#include "DHT11.h"

static uint32_t pMillis, cMillis;        // Read time for(timeout).

void DHT11_Init(DHT11_DataTypedef *DHT, TIM_HandleTypeDef *Timer, GPIO_TypeDef *DHT_Port, uint16_t DHT_Pin)
{
    DHT->DHT_TIM = Timer;
    DHT->DHT_PORT = DHT_Port;
    DHT->DHT_PIN = DHT_Pin;
    HAL_TIM_Base_Start(DHT->DHT_TIM);   // Start Timer.
}

static void microDelay(DHT11_DataTypedef *DHT, uint16_t delay)
{
    __HAL_TIM_SET_COUNTER(DHT->DHT_TIM, 0);                // Reset count.
    while (__HAL_TIM_GET_COUNTER(DHT->DHT_TIM) < delay);   // Wait count.
}

static uint8_t DHT11_Start(DHT11_DataTypedef *DHT)
{
    uint8_t Response = 0;
    GPIO_InitTypeDef GPIO_InitStructPrivate = {0};

    GPIO_InitStructPrivate.Pin = DHT->DHT_PIN;
    GPIO_InitStructPrivate.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStructPrivate.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStructPrivate.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(DHT->DHT_PORT, &GPIO_InitStructPrivate);  // set the pin as output

    /* Wait DHT11 response */
    HAL_GPIO_WritePin(DHT->DHT_PORT, DHT->DHT_PIN, GPIO_PIN_RESET);   // pull the pin low
    HAL_Delay(20); 												      // wait for 20ms
    HAL_GPIO_WritePin(DHT->DHT_PORT, DHT->DHT_PIN, GPIO_PIN_SET);     // pull the pin high
    microDelay(DHT, 30);										      // wait for 30us

    /* Reponse DHT11 */
    GPIO_InitStructPrivate.Mode = GPIO_MODE_INPUT;                    // -> Listen Response
    GPIO_InitStructPrivate.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(DHT->DHT_PORT, &GPIO_InitStructPrivate);			  // set the pin as input

    microDelay(DHT, 40);
    if (!HAL_GPIO_ReadPin(DHT->DHT_PORT, DHT->DHT_PIN)) {             // '0'
        microDelay(DHT, 80);         // DHT11 kéo LOW 80µs
        if (HAL_GPIO_ReadPin(DHT->DHT_PORT, DHT->DHT_PIN)) Response = 1;    // DHT11 nhả HIGH 80µs
    }

    pMillis = HAL_GetTick();   // Start Time
    cMillis = HAL_GetTick();   // Current Time
    // Timeout: DHT11 bit '1' -> 2ms
    while (HAL_GPIO_ReadPin(DHT->DHT_PORT, DHT->DHT_PIN) && pMillis + 2 > cMillis) {
        cMillis = HAL_GetTick();    // Update cMillis
    }
    return Response;     // '1' -> True
}

static uint8_t DHT11_Read(DHT11_DataTypedef *DHT)
{
    uint8_t a, b = 0;               // b: chứa dữ liệu 8 bit sau khi đọc.
    for (a = 0; a < 8; a++) {       // Read 1 byte(8 bit)
        pMillis = HAL_GetTick();
        cMillis = HAL_GetTick();
        while (!HAL_GPIO_ReadPin(DHT->DHT_PORT, DHT->DHT_PIN) && pMillis + 2 > cMillis)
        {   // wait pin to go HIGH or OUT -> (>= Timeout: 2ms)
            cMillis = HAL_GetTick();
        }
        microDelay(DHT, 40);									// wait for 40 us
        if (!HAL_GPIO_ReadPin(DHT->DHT_PORT, DHT->DHT_PIN))     // if the pin is LOW
            b &= ~(1 << (7 - a));								// Clear Bit at a = (0)
        else													// if pin is HIGH
            b |= (1 << (7 - a));                                // Set Bit at a = (1)

        pMillis = HAL_GetTick();
        cMillis = HAL_GetTick();
        while (HAL_GPIO_ReadPin(DHT->DHT_PORT, DHT->DHT_PIN) && pMillis + 2 > cMillis)
        {   // wait for the pin to go LOW → báo hiệu kết thúc bit.
            cMillis = HAL_GetTick();
        }
    }
    return b;
}

uint8_t DHT11_Read_Data(DHT11_DataTypedef *DHT)
{
	uint8_t RHI, RHD, TCI, TCD, SUM;

    if (DHT11_Start(DHT))
    {                            // Read data 40 bit.
        RHI = DHT11_Read(DHT);   // 8 bit: Humi int
        RHD = DHT11_Read(DHT);   // 8 bit: Humi float
        TCI = DHT11_Read(DHT);   // 8 bit: Temp int
        TCD = DHT11_Read(DHT);   // 8 bit: Temp float
        SUM = DHT11_Read(DHT);   // 8 bit: Checksum

        if (RHI + RHD + TCI + TCD == SUM) {		// True
//            DHT->Temperature = (float)TCI + (float)(TCD / 10.0);
//            DHT->Humidity = (float)RHI + (float)(RHD / 10.0);
            DHT->Temperature = TCI;
            DHT->Humidity = RHI;
            return 1; // OK
        }
    }
    return 0; // lỗi checksum hoặc không phản hồi
}
