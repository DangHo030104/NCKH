#include "sensor_manager.h"
#include "app_config.h"
#include "system_manager.h"
#include "DHT11.h"
#include "battery.h"

/* ----- Read Sensors ----- */

static DHT11_DataTypedef dht11;

static uint16_t SensorManager_ReadAdc(uint32_t channel)
{
    ADC_ChannelConfTypeDef config = {0};
    config.Channel = channel;
    config.Rank = ADC_REGULAR_RANK_1;
    config.SamplingTime = ADC_SAMPLETIME_71CYCLES_5;
    HAL_ADC_ConfigChannel(&hadc1, &config);

    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 100U); 				/* Chờ ADC conversion hoàn tất. */
    uint16_t value = (uint16_t)HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);

    return value;
}

static float SensorManager_SoilPercent(uint16_t adc, uint16_t dry, uint16_t wet)
{
    /* Trường hợp phổ biến: ADC khi khô > ADC khi ướt. */
    float moisture = ((float)dry - (float)adc) * 100.0f / ((float)dry - (float)wet);

    /* Giới hạn kết quả trong khoảng [0..100%] */
    if (moisture > 100.0f) moisture = 100.0f;
    if (moisture < 0.0f) moisture = 0.0f;

    return moisture;
}

void SensorManager_Init(void)
{
    DHT11_Init(&dht11, &htim1, DHT11_PORT, DHT11_PIN);
    Battery_Init(&hadc1);
}

void SensorManager_ReadAll(void)
{
    /* 1. SOIL MOISTURE: đọc ADC raw rồi chuyển sang phần trăm độ ẩm. */
    soil1_adc = SensorManager_ReadAdc(ADC_CHANNEL_0);
    soil2_adc = SensorManager_ReadAdc(ADC_CHANNEL_1);

    sm1 = SensorManager_SoilPercent(soil1_adc, SOIL1_ADC_DRY, SOIL1_ADC_WET);
    sm2 = SensorManager_SoilPercent(soil2_adc, SOIL2_ADC_DRY, SOIL2_ADC_WET);

    /* 2. DHT11 */
    if (DHT11_Read_Data(&dht11))
    {
        temp = dht11.Temperature;
        humi = dht11.Humidity;
    }

    /* 3. BATTERY: chuyển ADC sang channel đo pin. */
    ADC_ChannelConfTypeDef config = {0};
    config.Channel = BATTERY_ADC_CHANNEL;
    config.Rank = ADC_REGULAR_RANK_1;
    config.SamplingTime = ADC_SAMPLETIME_71CYCLES_5;
    HAL_ADC_ConfigChannel(&hadc1, &config);

    /* Đọc điện áp rồi chuyển sang phần trăm pin. */
    battery_voltage = Battery_ReadVoltage();
    battery_percent = Battery_GetPercent(battery_voltage);

    /* Đánh dấu snapshot sensor mới đã sẵn sàng. */
    last_sensor_update_tick = HAL_GetTick();
    sensor_data_valid = 1;
}
