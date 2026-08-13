#ifndef INC_UPDM010UB_PARSER_H_
#define INC_UPDM010UB_PARSER_H_

#include "main.h"
#include "app.h"

/* 패킷 규격 정의 (총 32바이트) */
#define DUST_MAX_BUF_LEN      32
#define DUST_HEADER0          0x42
#define DUST_HEADER1          0x4D

/* 데이터 인덱스 (데이터시트 Byte 번호와 동일) */
#define IDX_PM1_H             4
#define IDX_PM1_L             5
#define IDX_PM25_H            6
#define IDX_PM25_L            7
#define IDX_PM10_H            8
#define IDX_PM10_L            9

/* 체크섬 위치 (데이터시트 Byte 30, 31) */
#define IDX_VERIFY_H          30
#define IDX_VERIFY_L          31

/* 함수 선언 */
uint8_t DUST_Init(UART_HandleTypeDef *huart);
void    DUST_UART_ISR(UART_HandleTypeDef *huart);
uint8_t DUST_GetReadyData(DustData *outData);

#endif
