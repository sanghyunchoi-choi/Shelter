/*
 * protocol.h
 *
 *  Created on: Mar 11, 2026
 *      Author: cshss
 */

#ifndef INC_PROTOCOL_H_
#define INC_PROTOCOL_H_

#include "main.h"

/* 전송 패킷 상수 */
#define PKT_STX          0x02
#define PKT_ETX          0x03
#define PKT_LEN_REPORT   30     // BB 명령어 전송 길이
#define PKT_CMD_REPORT   0xBB   // 보고용 커맨드

/* 함수 선언 */
uint8_t Calculate_BCC(uint8_t *data, uint16_t len);
void Build_Report_Packet(uint8_t *buf, uint8_t id, uint8_t *relays, int16_t *currents);


#endif /* INC_PROTOCOL_H_ */
