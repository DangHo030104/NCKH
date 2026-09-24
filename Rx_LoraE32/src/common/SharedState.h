#ifndef RX_LORAE32_COMMON_SHAREDSTATE_H
#define RX_LORAE32_COMMON_SHAREDSTATE_H

#include "DataTypes.h"

DashboardStatus readDashboardStatus(void);
void setMqttDisplayState(bool online);
void setWiFiDisplayState(bool online);

#endif // RX_LORAE32_COMMON_SHAREDSTATE_H
