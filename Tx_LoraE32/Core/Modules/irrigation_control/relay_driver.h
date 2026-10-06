#ifndef RELAY_DRIVER_H
#define RELAY_DRIVER_H

#include "app_types.h"

void RelayDriver_StartZone1(void);
void RelayDriver_StartZone2(void);
void RelayDriver_StopAll(void);
void RelayDriver_StopCurrent(RelayOwner owner);

#endif
