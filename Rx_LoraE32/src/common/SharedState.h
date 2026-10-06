#ifndef RX_LORAE32_COMMON_SHAREDSTATE_H
#define RX_LORAE32_COMMON_SHAREDSTATE_H

#include "DataTypes.h"

DashboardStatus readDashboardStatus(void);
void setMqttDisplayState(bool online);
void setWiFiDisplayState(bool online);
uint32_t nextZoneCommand(uint8_t zone);
uint32_t readLatestZoneCommand(uint8_t zone);

#endif // RX_LORAE32_COMMON_SHAREDSTATE_H
