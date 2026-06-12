/*
 * smart_home_system.c
 *
 *  Created on: May 19, 2026
 *      Author: Admin
 */

#include "smart_home_system.h"

#include "sensor_manager.h"
#include "touch_button.h"
#include "device_control.h"
#include "user_interface.h"

void smartHome_Init(void)
{
	sensorManagerInit();
	userInterfaceInit();
}

void smartHome_Run(void)
{
	sensorManagerUpdate();
	touchButtonUpdate();
	deviceControlUpdate();
	userInterfaceUpdate();
}
