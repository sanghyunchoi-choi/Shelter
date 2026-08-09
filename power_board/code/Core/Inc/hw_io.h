/*
 * hw_io.h
 *
 *  Created on: Mar 11, 2026
 *      Author: cshss
 */

#ifndef INC_HW_IO_H_
#define INC_HW_IO_H_

#include "main.h"

void HW_Relay_Set(uint8_t ch, uint8_t state);
uint8_t HW_Get_BoardID(void);
void HW_RS485_TX_Mode(uint8_t is_tx);
void App_Relay_Test(void);
#endif /* INC_HW_IO_H_ */
