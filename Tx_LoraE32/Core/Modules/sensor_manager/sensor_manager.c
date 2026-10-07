#include "sensor_manager.h"
#include "app_config.h"
#include "system_manager.h"
#include "debug_console.h"
#include "DHT11.h"
#include "battery.h"

/* ----- Read Sensors ----- */

static DHT11_DataTypedef dht11;

static uint8_t SensorManager_ReadAdc(uint32_t channel, uint16_t *average)
{
    if (average == NULL) return 0;

    ADC_ChannelConfTypeDef config = {0};
    config.Channel = channel;
    config.Rank = ADC_REGULAR_RANK_1;
    config.SamplingTime = SENSOR_ADC_SAMPLING_TIME;

    if (HAL_ADC_ConfigChannel(&hadc1, &config) != HAL_OK)
    {
        DebugConsole_Print("[ADC] Channel configuration failed\r\n");
        return 0;
    }

    uint32_t sum = 0;
    uint16_t valid_samples = 0;

    /* Average several conversions to reduce relay, pump and radio noise. */
    for (uint16_t sample = 0; sample < SOIL_ADC_SAMPLE_COUNT; sample++)
    {
        if (HAL_ADC_Start(&hadc1) != HAL_OK) continue;

        if (HAL_ADC_PollForConversion(&hadc1, 100) == HAL_OK)
        {
            sum += HAL_ADC_GetValue(&hadc1);
            valid_samples++;
        }

        HAL_ADC_Stop(&hadc1);
    }

    if (valid_samples == 0)
    {
        DebugConsole_Print("[ADC] No valid soil samples\r\n");
        return 0;
    }

    /* Round the integer average instead of always truncating it. */
    *average = (uint16_t)((sum + (valid_samples / 2)) / valid_samples);
    return 1;
}

static float SensorManager_SoilPercent(uint16_t adc, uint16_t dry, uint16_t wet)
{
    /* Common sensor direction: dry ADC is higher than wet ADC. */
    float moisture = ((float)dry - (float)adc) * 100.0f / ((float)dry - (float)wet);

    if (moisture > 100.0f) moisture = 100.0f;
    if (moisture < 0.0f) moisture = 0.0f;

    return moisture;
}

void SensorManager_Init(void)
{
    /* STM32F1 ADC calibration removes converter offset before the first sample. */
    if (HAL_ADCEx_Calibration_Start(&hadc1) == HAL_OK)
        DebugConsole_Print("[ADC] Calibration completed\r\n");
    else
        DebugConsole_Print("[ERROR] ADC calibration failed\r\n");

    DHT11_Init(&dht11, &htim1, DHT11_PORT, DHT11_PIN);
    Battery_Init(&hadc1);
}

void SensorManager_ReadAll(void)
{
    /* 1. SOIL MOISTURE: average raw ADC samples, then convert to percent. */
    uint16_t soil1_average = 0;
    uint16_t soil2_average = 0;
    uint8_t soil1_valid = SensorManager_ReadAdc(ADC_CHANNEL_0, &soil1_average);
    uint8_t soil2_valid = SensorManager_ReadAdc(ADC_CHANNEL_1, &soil2_average);

    if (soil1_valid)
    {
        soil1_adc = soil1_average;
        sm1 = SensorManager_SoilPercent(soil1_adc, SOIL1_ADC_DRY, SOIL1_ADC_WET);
    }

    if (soil2_valid)
    {
        soil2_adc = soil2_average;
        sm2 = SensorManager_SoilPercent(soil2_adc, SOIL2_ADC_DRY, SOIL2_ADC_WET);
    }

    /* 2. DHT11 */
    if (DHT11_Read_Data(&dht11))
    {
        temp = dht11.Temperature;
        humi = dht11.Humidity;
    }

    /* 3. BATTERY: switch ADC to the battery measurement channel. */
    ADC_ChannelConfTypeDef config = {0};
    config.Channel = BATTERY_ADC_CHANNEL;
    config.Rank = ADC_REGULAR_RANK_1;
    config.SamplingTime = SENSOR_ADC_SAMPLING_TIME;

    if (HAL_ADC_ConfigChannel(&hadc1, &config) == HAL_OK)
    {
        battery_voltage = Battery_ReadVoltage();
        battery_percent = Battery_GetPercent(battery_voltage);
    }
    else
    {
        DebugConsole_Print("[ADC] Battery channel configuration failed\r\n");
    }

    /* AUTO may act only when both soil channels produced a valid average. */
    last_sensor_update_tick = HAL_GetTick();
    sensor_data_valid = soil1_valid && soil2_valid;
}
