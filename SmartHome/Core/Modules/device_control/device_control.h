/*
 * device_control.h
 *
 *  Created on: May 19, 2026
 *      Author: Admin
 */

#ifndef _DEVICE_CONTROL_H
#define _DEVICE_CONTROL_H

#include "main.h"

void deviceControlUpdate(void);

extern uint8_t led1State;
extern uint8_t led2State;
extern uint8_t fanState;

extern uint8_t led1Mode;
extern uint8_t led2Mode;
extern uint8_t fanMode;

extern uint16_t dark_Threshold;
extern uint16_t bright_Threshold;

extern uint16_t tempOnFan;
extern uint16_t tempOffFan;

#endif /* _DEVICE_CONTROL_H */
