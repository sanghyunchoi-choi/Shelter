/*
 * app_loop.h
 *
 *  Created on: Mar 4, 2026
 *      Author: cshss
 */

#ifndef INC_APP_LOOP_H_
#define INC_APP_LOOP_H_


/* 장치별 온라인 여부 확인 함수 */
bool Is_PowerBoard_Online(void);
bool Is_AP_Online(void);
bool Is_AC_Online(void);
bool Is_Sensor_Online(DustData *dust, THData *th);


void RLY_SetStatus(uint8_t ch, bool state);
bool RLY_GetStatus(uint8_t ch);
void RLY_All_Reset(void);
void RLY_SetButtonControl(bool is_entrance, bool enable);

void SCAN_External_Inputs(void);
void SCAN_Switch_Configuration(void);
#endif /* INC_APP_LOOP_H_ */
