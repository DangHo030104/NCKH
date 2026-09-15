#ifndef RX_LORAE32_COMMON_SHAREDSTATE_H
#define RX_LORAE32_COMMON_SHAREDSTATE_H

#include "DataTypes.h"

DashboardStatus readDashboardStatus(void);
void setCommandDisplayState(CommandDisplayState state, uint8_t attempt);
void setCommandDetails(uint8_t zone, bool irrigationOn);
void setMqttDisplayState(bool online);
void setWiFiDisplayState(bool online);

#endif // RX_LORAE32_COMMON_SHAREDSTATE_H
