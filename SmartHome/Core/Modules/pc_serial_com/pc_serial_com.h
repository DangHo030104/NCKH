/*
 * pc_serial_com.h
 *
 *  Created on: May 19, 2026
 *      Author: Admin
 */

#ifndef _PC_SERIAL_COM_H_
#define _PC_SERIAL_COM_H_

#include "main.h"

void PC_Serial_ProcessCommand(uint8_t cmd, UART_HandleTypeDef *huart);

#endif /* _PC_SERIAL_COM_H_ */
