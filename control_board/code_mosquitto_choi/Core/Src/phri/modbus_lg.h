#ifndef INC_MODBUS_LG_H_
#define INC_MODBUS_LG_H_

#include "main.h"
#include <stdint.h>
#include <stdbool.h>

/* --- [1] Modbus 공통 설정 --- */
#define MODBUS_SLAVE_ID         0x01
#define MODBUS_ON               0xFF00
#define MODBUS_OFF              0x0000

/* --- [2] LG 실내기 레지스터 주소 (PDF v3 중앙제어 맵, 실내기 #1) --- */
#define ADDR_AC_POWER           0x0000      // Coil 00001 (운전/정지)
#define ADDR_AC_CONN            0x0000      // Discrete 10001 (실내기 연결)
#define ADDR_SET_MODE           0x0000      // Holding 40001 (운전 모드)
#define ADDR_SET_WIND           0x0001      // Holding 40002 (바람 세기)
#define ADDR_SET_TEMP           0x0002      // Holding 40003 (설정 온도)
#define ADDR_ERROR_CODE         0x0000      // Input 30001 (에러 코드)
#define ADDR_INDOOR_TEMP        0x0001      // Input 30002 (실내 온도)
#define HOLDING_READ_COUNT      3           // 40001~40003 일괄 조회

/* --- [3] 제어 파라미터 정의 (실내기 1대 — LG Modbus 중앙제어 #1) --- */
typedef enum {
    AC_MODE_COOL = 0, AC_MODE_DRY = 1, AC_MODE_FAN = 2, AC_MODE_AUTO = 3, AC_MODE_HEAT = 4
} LG_AC_MODE;

typedef enum {
    AC_WIND_LOW = 1, AC_WIND_MID = 2, AC_WIND_HIGH = 3, AC_WIND_AUTO = 4,
    AC_WIND_ULTRA_LOW = 5, AC_WIND_POWER = 6
} LG_AC_WIND;

/* --- [4] 상태 머신 및 함수 코드 --- */
typedef enum {
    MODBUS_SM_SLAVE_ID, MODBUS_SM_FUNC_CODE, MODBUS_SM_BYTE_COUNT,
    MODBUS_SM_ADDRESS, MODBUS_SM_DATA, MODBUS_SM_CRC
} MODBUS_SM;

typedef enum {
    FUNC_READ_COIL = 0x01, FUNC_READ_DISCRETE = 0x02, FUNC_READ_HOLDING = 0x03,
    FUNC_READ_INPUT = 0x04, FUNC_WRITE_COIL = 0x05, FUNC_WRITE_HOLDING = 0x06
} LG_MODBUS_FUNC;

/* --- [5] 주요 API 함수 --- */
void Modbus_Init(void);
void Modbus_RestartRxIT(void);
void ModbusSmInput(uint8_t input);
void Modbus_UART_ISR(UART_HandleTypeDef *huart);
void getACStatus(void); // 전체 상태 폴링

// Setter API (제어용) — 0=Modbus 성공, -1=실패
int setACPower(bool on);
int setACMode(uint8_t mode);     /* 0=COOL .. 4=HEAT (LG 40001) */
int setACSpeed(uint8_t speed);
int setACTemperature(uint16_t temp_x10);
int setACWindSpeed(uint8_t speed); /* LG 40002: 1~6 (5=미약, 6=파워) */
#endif
