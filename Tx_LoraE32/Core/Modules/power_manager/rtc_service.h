#ifndef RTC_SERVICE_H
#define RTC_SERVICE_H

#include <stdint.h>

uint8_t RtcService_EnsureAlarmAfterSeconds(uint8_t seconds);
void RtcService_CancelAlarm(void);
uint8_t RtcService_IsAlarmArmed(void);
uint32_t RtcService_GetWakeIrqCount(void);

#endif
