#include "rtc_service.h"
#include "debug_console.h"
#include "system_manager.h"
#include <stdio.h>

/* ----- Quản lý RTC Alarm ----- */

void RtcService_PrintTime(void)
{
    RTC_TimeTypeDef time;
    RTC_DateTypeDef date;

    HAL_RTC_GetTime(&hrtc, &time, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &date, RTC_FORMAT_BIN);

    char msg[100];
    snprintf(msg, sizeof(msg), "RTC %02d:%02d:%02d - %02d/%02d/20%d\r\n",
             time.Hours, time.Minutes, time.Seconds, date.Date, date.Month, date.Year);

    DebugConsole_Print(msg);
}

void RtcService_SetAlarmAfterSeconds(uint8_t seconds)
{
    RTC_TimeTypeDef time;
    RTC_AlarmTypeDef alarm = {0};
    
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
    HAL_RTC_SetAlarm_IT(&hrtc, &alarm, RTC_FORMAT_BIN);
    DebugConsole_Print("[RTC] Alarm Set!\r\n");
}

void HAL_RTC_AlarmAEventCallback(RTC_HandleTypeDef *rtc)
{
    rtc_wakeup_flag = 1;
}
