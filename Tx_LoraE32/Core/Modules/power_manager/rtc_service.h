#ifndef RTC_SERVICE_H
#define RTC_SERVICE_H

#include <stdint.h>

void RtcService_PrintTime(void);
void RtcService_SetAlarmAfterSeconds(uint8_t seconds);

#endif
