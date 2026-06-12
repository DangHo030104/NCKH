/*
 * sensor_manager.c
 *
 *  Created on: May 19, 2026
 *      Author: Admin
 */

#include "sensor_manager.h"

#include "DHT11.h"

extern ADC_HandleTypeDef hadc1;
extern TIM_HandleTypeDef htim1;

DHT11_DataTypedef DHT11;

uint32_t last_readLDR = 0;
uint32_t last_readDHT = 0;

uint16_t temp = 0;
uint16_t humi = 0;
uint16_t ldrValue = 0;

uint8_t radarBuffer[64];
uint8_t radarIndex = 0;

uint8_t presenceState = 0;
uint8_t movingTarget = 0;
uint8_t staticTarget = 0;

static uint16_t Read_LDR(void);
static void Parse_LD2410_Frame(uint8_t *buf, uint8_t len);

void sensorManagerInit(void)
{
    DHT11_Init(&DHT11, &htim1, GPIOA, GPIO_PIN_1);
}

void sensorManagerUpdate(void)
{
	// Read LDR 1s
    if(HAL_GetTick() - last_readLDR >= 1000)
    {
        last_readLDR = HAL_GetTick();

        uint32_t sum = 0;
        for(int i = 0; i < 10; i++)
        {
            sum += Read_LDR();
        }

        ldrValue = sum / 10;
    }

    // Read DHT11 2s
    if(HAL_GetTick() - last_readDHT >= 2000)
    {
        last_readDHT = HAL_GetTick();

        if(DHT11_Read_Data(&DHT11))
        {
            temp = DHT11.Temperature;
            humi = DHT11.Humidity;
        }
    }
}

static uint16_t Read_LDR(void)
{
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 100);
    uint16_t value = HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);

    return value;
}

void Sensor_Radar_UART_Callback(uint8_t byte)
{
    radarBuffer[radarIndex++] = byte;

    if(radarIndex >= sizeof(radarBuffer))
    {
        radarIndex = 0;
    }

    /* LD2410 frame thuong kết thúc bằng F8 F7 F6 F5
    / -> Khi nhận đủ 4 byte cuối trùng sequence này → coi như frame đã hoàn chỉnh */

    if(radarIndex >= 4)
    {
        if(radarBuffer[radarIndex - 4] == 0xF8 &&
           radarBuffer[radarIndex - 3] == 0xF7 &&
           radarBuffer[radarIndex - 2] == 0xF6 &&
           radarBuffer[radarIndex - 1] == 0xF5)
        {
            Parse_LD2410_Frame(radarBuffer, radarIndex);
            radarIndex = 0;
        }
    }
}

// Hàm phân tích dữ liệu LD2410C
static void Parse_LD2410_Frame(uint8_t *buf, uint8_t len)
{
	/* Frame radar HLK-LD2410C có độ dài tối thiểu ~20 byte */
    if(len < 20) return;	//

    /* Kiểm tra header frame bắt đầu bằng F4 F3 F2 F1 */
    if(buf[0] != 0xF4 || buf[1] != 0xF3 || buf[2] != 0xF2 || buf[3] != 0xF1)
    {
        return;
    }

    /*
     * Dữ liệu cơ bản LD2410 thư�?ng có dạng:
     * Target state:
     * 0x00 = không có nguoi
     * 0x01 = moving target
     * 0x02 = static target
     * 0x03 = moving + static
     */

    uint8_t targetState = buf[8];

    presenceState = (targetState != 0x00) ? 1 : 0;
    movingTarget = (targetState == 0x01 || targetState == 0x03) ? 1 : 0;
    staticTarget = (targetState == 0x02 || targetState == 0x03) ? 1 : 0;
}
