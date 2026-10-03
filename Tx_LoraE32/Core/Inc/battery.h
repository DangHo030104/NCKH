/*
 * battery.h
 *
 *  Created on: Sep 16, 2026
 *      Author: Admin
 */

#ifndef BATTERY_H
#define BATTERY_H

#include "main.h" // Chứa các khai báo HAL và cấu hình board

/* ===== CẤU HÌNH THÔNG SỐ PHẦN CỨNG ===== */
#define VREF 3.3f                   // Điện áp tham chiếu của STM32
#define ADC_MAX_VAL 4095.0f         // Độ phân giải ADC 12-bit
#define VOLTAGE_DIVIDER_RATIO 2.112f // Hệ số bù trừ đo thực tế (4.0V / 1.9V)
#define BATTERY_NUM_SAMPLES 100      // Tăng lên 50 lần lấy mẫu ADC để lọc nhiễu tốt hơn

/* ===== NGUYÊN MẪU HÀM ===== */
void Battery_Init(ADC_HandleTypeDef *hadc);
float Battery_ReadVoltage(void);
uint8_t Battery_GetPercent(float voltage);

#endif /* BATTERY_H */
