#include "rtc_service.h"
#include "debug_console.h"
#include "system_manager.h"
#include <stdio.h>

/* ----- Quản lý RTC Alarm ----- */

static volatile uint32_t rtc_wake_irq_count = 0;
static volatile uint8_t rtc_alarm_armed = 0;    // Flag theo dõi trạng thái của RTC Alarm (1 = đã đặt, 0 = chưa đặt)

uint8_t RtcService_EnsureAlarmAfterSeconds(uint8_t seconds)
{
    RTC_TimeTypeDef time;
    RTC_AlarmTypeDef alarm = {0};

    /* Keep the current deadline. A LoRa wake must not postpone the RTC schedule. */
    if (rtc_alarm_armed)
    {
        DebugConsole_Print("[RTC] Existing alarm kept; LoRa wake did not postpone deadline\r\n");
        return 1;
    }
    
    /* Xóa RTC Alarm cũ trước khi đặt alarm mới. */
    HAL_RTC_DeactivateAlarm(&hrtc, RTC_ALARM_A);
    __HAL_RTC_ALARM_CLEAR_FLAG(&hrtc, RTC_FLAG_ALRAF);
    __HAL_RTC_ALARM_EXTI_CLEAR_FLAG();

    HAL_RTC_GetTime(&hrtc, &time, RTC_FORMAT_BIN);

    uint32_t total = time.Seconds + seconds;
    alarm.AlarmTime.Seconds = total % 60U;
    total = time.Minutes + total / 60U;
    alarm.AlarmTime.Minutes = total % 60U;
    alarm.AlarmTime.Hours = (time.Hours + total / 60U) % 24U;
    alarm.Alarm = RTC_ALARM_A;

    /* Dùng alarm interrupt để đánh thức STM32 khỏi STOP mode. */
    HAL_StatusTypeDef status = HAL_RTC_SetAlarm_IT(&hrtc, &alarm, RTC_FORMAT_BIN);
    rtc_alarm_armed = (status == HAL_OK) ? 1 : 0;

    char msg[96];
    snprintf(msg, sizeof(msg),
             "[RTC] Alarm +%u s -> %02u:%02u:%02u | status=%s\r\n",
             (unsigned int)seconds,
             (unsigned int)alarm.AlarmTime.Hours,
             (unsigned int)alarm.AlarmTime.Minutes,
             (unsigned int)alarm.AlarmTime.Seconds,
             status == HAL_OK ? "OK" : "ERROR");
    DebugConsole_Print(msg);

    return rtc_alarm_armed;
}

void RtcService_CancelAlarm(void)
{
    if (!rtc_alarm_armed) return;

    if (HAL_RTC_DeactivateAlarm(&hrtc, RTC_ALARM_A) != HAL_OK)
    {
        DebugConsole_Print("[ERROR] RTC alarm cancellation failed\r\n");
        return;
    }

    __HAL_RTC_ALARM_CLEAR_FLAG(&hrtc, RTC_FLAG_ALRAF);
    __HAL_RTC_ALARM_EXTI_CLEAR_FLAG();
    
    rtc_alarm_armed = 0;
    DebugConsole_Print("[RTC] Alarm cancelled because periodic AUTO wake is not required\r\n");
}

uint8_t RtcService_IsAlarmArmed(void)
{
    return rtc_alarm_armed;
}

void HAL_RTC_AlarmAEventCallback(RTC_HandleTypeDef *rtc)
{
    if (rtc != NULL && rtc->Instance == RTC)
    {
        /* Alarm A is one-shot. The next AUTO interval starts after this event. */
        rtc_alarm_armed = 0;
        rtc_wakeup_flag = 1;
        rtc_wake_irq_count++;
    }
}

uint32_t RtcService_GetWakeIrqCount(void)
{
    return rtc_wake_irq_count;
}
