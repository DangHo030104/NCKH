/*
 * battery.c
 *
 *  Created on: Sep 16, 2026
 *      Author: Admin
 */

#include "battery.h"

// Con trỏ lưu trữ bộ ADC được sử dụng
static ADC_HandleTypeDef *bat_hadc;

// --- CÁC BIẾN TĨNH CHO BỘ LỌC PHẦN MỀM ---
static float stable_voltage = 0.0f;
static uint8_t is_first_read = 1;
static uint8_t outlier_count = 0;
static float stable_percent = -1.0f; // Bắt đầu bằng -1 để nhận biết lần đo đầu tiên

// --- CẤU HÌNH BỘ LỌC ---
#define OUTLIER_THRESHOLD 0.3f       // Bỏ qua nếu giá trị mới chênh lệch > 0.3V so với mức ổn định
#define MAX_OUTLIER_COUNT 5          // Nếu lệch liên tục 5 lần, chấp nhận giá trị mới
#define FILTER_ALPHA 0.05f           // Hệ số làm mượt điện áp (0.05 = rất mượt, hơi trễ nhẹ)
#define PERCENT_FILTER_ALPHA 0.1f    // Hệ số làm mượt phần trăm pin

// Bảng ánh xạ điện áp và phần trăm (Cạn 3.3V, Đầy 4.2V)
static const float voltage_table[] = {3.3f, 3.4f, 3.5f, 3.6f, 3.7f, 3.8f, 3.9f, 4.0f, 4.1f, 4.2f};
static const uint8_t percent_table[] = {0,    5,    10,   20,   30,   45,   60,   80,   90,   100};
static const int TABLE_SIZE = sizeof(voltage_table) / sizeof(voltage_table[0]);

// Khởi tạo thư viện
void Battery_Init(ADC_HandleTypeDef *hadc) {
    bat_hadc = hadc;
    is_first_read = 1;
    stable_percent = -1.0f;
}

// Hàm đọc và lọc nhiễu điện áp
float Battery_ReadVoltage(void) {
    uint32_t adc_sum = 0;
    uint16_t valid_samples = 0;

    // 1. Lấy mẫu nhiều lần để trung bình phần cứng
    for (int i = 0; i < BATTERY_NUM_SAMPLES; i++) {
        HAL_ADC_Start(bat_hadc);
        if (HAL_ADC_PollForConversion(bat_hadc, 10) == HAL_OK) {
            adc_sum += HAL_ADC_GetValue(bat_hadc);
            valid_samples++;
        }
        HAL_ADC_Stop(bat_hadc);
    }

    if (valid_samples == 0) return stable_voltage;

    // Tính điện áp thô hiện tại
    float adc_avg = (float)adc_sum / valid_samples;
    float v_pin = (adc_avg / ADC_MAX_VAL) * VREF;
    float raw_voltage = v_pin * VOLTAGE_DIVIDER_RATIO;

    // 2. Thuật toán Lọc Loại bỏ nhiễu đột biến (Outlier) & Làm mượt (EMA)
    if (is_first_read) {
        stable_voltage = raw_voltage;
        is_first_read = 0;
    } else {
        float diff = raw_voltage - stable_voltage;
        if (diff < 0.0f) diff = -diff;

        if (diff > OUTLIER_THRESHOLD) {
            outlier_count++;
            if (outlier_count >= MAX_OUTLIER_COUNT) {
                stable_voltage = raw_voltage;
                outlier_count = 0;
            }
        } else {
            outlier_count = 0;
            stable_voltage = (stable_voltage * (1.0f - FILTER_ALPHA)) + (raw_voltage * FILTER_ALPHA);
        }
    }

    return stable_voltage;
}

// Tính phần trăm pin và lọc trung bình lớp thứ 2
uint8_t Battery_GetPercent(float voltage) {
    float raw_percent = 0.0f;

    // 1. Tính % thô dựa trên điện áp
    if (voltage >= 4.2f) {
        raw_percent = 100.0f;
    } else if (voltage <= 3.3f) {
        raw_percent = 0.0f;
    } else {
        // Nội suy tuyến tính
        for (int i = 0; i < TABLE_SIZE - 1; i++) {
            if (voltage >= voltage_table[i] && voltage <= voltage_table[i+1]) {
                float v_range = voltage_table[i+1] - voltage_table[i];
                float p_range = percent_table[i+1] - percent_table[i];
                float v_offset = voltage - voltage_table[i];

                raw_percent = percent_table[i] + ((v_offset / v_range) * p_range);
                break;
            }
        }
    }

    // 2. Lọc trung bình mượt cho riêng số phần trăm
    if (stable_percent < 0.0f) {
        stable_percent = raw_percent; // Lần đầu bật máy, lấy luôn giá trị thật
    } else {
        // Làm mượt từ từ, chống nhảy số ranh giới
        stable_percent = (stable_percent * (1.0f - PERCENT_FILTER_ALPHA)) + (raw_percent * PERCENT_FILTER_ALPHA);
    }

    // 3. Trả về số nguyên (cộng 0.5f để làm tròn toán học)
    return (uint8_t)(stable_percent + 0.5f);
}

