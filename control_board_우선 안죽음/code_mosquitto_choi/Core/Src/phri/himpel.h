#ifndef INC_HIMPEL_H_
#define INC_HIMPEL_H_

#include "main.h"
#include "config.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* --- [1] 프로토콜 공통 규격 --- */
#define HIMPEL_STX          0x53    // 'S'
#define HIMPEL_HEADER       0x20    // Space
#define HIMPEL_EOP          0x45    // 'E'
#define HIMPEL_DATA_SIZE    17      // 데이터 영역 크기
#define HIMPEL_PACKET_SIZE  24      // 전체 패킷 크기 (STX~EOP)
#define HIMPEL_PKT_DATA_OFF 5         // [STX][HD][DA][SA][CMD] 다음 17바이트 DATA
#define RX_BUFFER_SIZE      256
#define RETRY_COUNT         SHELTER_RS485_ACK_RETRIES


/* --- [2] 장치 ID 및 명령 정의 --- */
typedef enum {
    ID_GROUP      = 0xA0,
    ID_SINGLE     = 0xA1,           // 일반 전열교환기 (Standalone)
    ID_WINDOW     = 0xC1,           // 창문형 모델
    ID_CONTROL    = 0xF0            // 마스터 제어기 (STM32 측)
} HIMPEL_ID;

typedef enum {
    CMD_REQ_STATUS = 0x01,          // 상태 요청
    CMD_CONTROL    = 0x02,          // 제어 명령
    CMD_ADMIN      = 0x03           // 관리자 설정
} HIMPEL_CMD;

/* --- [3] 운전 모드 및 풍량 정의 (Enum으로 관리하여 실수 방지) --- */
typedef enum {
    FAN_MODE_VENT = 0,              // 환기
    FAN_MODE_AUTO,                  // 자동
    FAN_MODE_CLEAN,                 // 공기청정
    FAN_MODE_BYPASS,                // 바이패스
    FAN_MODE_HEATER,                // 히터 운전
    FAN_MODE_PROTECT                // 장비 보호
} HIMPEL_MODE;

typedef enum {
    FAN_SPEED_OFF   = 0,
    FAN_SPEED_LOW   = 1,
    FAN_SPEED_MID   = 2,
    FAN_SPEED_HIGH  = 3,
    FAN_SPEED_TURBO = 4
} HIMPEL_SPEED;

/* --- [4] 통신 상태 머신 정의 --- */
typedef enum {
    HIMPEL_SM_STX, HIMPEL_SM_HD, HIMPEL_SM_DA, HIMPEL_SM_SA,
    HIMPEL_SM_CMD, HIMPEL_SM_DATA, HIMPEL_SM_FRC, HIMPEL_SM_EOP
} HIMPEL_SM;

/* --- [5] 데이터 구조체 (17바이트 정밀 매핑) --- */
#pragma pack(push, 1)

/** [수신] 상태 메시지 구조체 (Slave -> Master) */
typedef struct {
    // DATA 0: 기본 상태
    uint8_t ubUV:1; uint8_t ubAirClean:1; uint8_t ubBypass:1; uint8_t ubNegPress:1;
    uint8_t ubPosPress:1; uint8_t ubVentilation:1; uint8_t ubAutomatic:1; uint8_t ubPower:1;

    // DATA 1: 풍량 및 상태 변화
    uint8_t ubSlaveChanged:1; uint8_t ubAlarmOff:1; uint8_t res1:1;
    uint8_t ubFanTurbo:1; uint8_t ubFanHigh:1; uint8_t ubFanMid:1; uint8_t ubFanLow:1; uint8_t ubFanOff:1;

    // DATA 2: 에러 및 모드
    uint8_t ubSAFanErr:1; uint8_t ubEAFanErr:1; uint8_t res2:2;
    uint8_t ubModeInterval:1; uint8_t ubModeHeater:1; uint8_t ubModeBase:1; uint8_t ubModeProtect:1;

    // DATA 3: 센서 에러 상세
    uint8_t ubInTempErr:1; uint8_t ubOutTempErr:1; uint8_t ubInHumiErr:1;
    uint8_t ubTVOCErr:1; uint8_t ubCO2Err:1; uint8_t ubDustErr:1; uint8_t ubPressErr:1; uint8_t ubCtrlErr:1;

    // DATA 4~6: 온습도 (Sign 1bit + Value 7bit)
    uint8_t inTempSign:1; uint8_t inTempVal:7;
    uint8_t outTempSign:1; uint8_t outTempVal:7;
    uint8_t inHumiSign:1; uint8_t inHumiVal:7;

    // DATA 7~16: 정밀 센서값 (High / Low Byte)
    uint8_t u8TVOC_H;       uint8_t u8TVOC_L;
    uint8_t u8CO2_H;        uint8_t u8CO2_L;
    uint8_t u8PM25_H;       uint8_t u8PM25_L;
    uint8_t u8PM10_H;       uint8_t u8PM10_L;
    uint8_t u8PM1_0_H;      uint8_t u8PM1_0_L;
} HimpelStatusMessage;

/** [송신] 제어 메시지 구조체 (Master -> Slave) */
typedef struct {
    // DATA 0: 전원 및 기본 제어
    uint8_t ubUV:1; uint8_t ubAirClean:1; uint8_t ubBypass:1; uint8_t ubNegPress:1;
    uint8_t ubPosPress:1; uint8_t ubVentilation:1; uint8_t ubAutomatic:1; uint8_t ubPower:1;

    // DATA 1: 풍량 설정
    uint8_t res1:3; uint8_t ubFanTurbo:1; uint8_t ubFanHigh:1; uint8_t ubFanMid:1; uint8_t ubFanLow:1; uint8_t ubFanOff:1;

    // DATA 2: 특수 모드 설정
    uint8_t res2:4; uint8_t ubModeInterval:1; uint8_t ubModeHeater:1; uint8_t ubModeBase:1; uint8_t ubModeProtect:1;

    // DATA 3: 알람/에러 제어
    uint8_t res3:5; uint8_t ubDevAlarmOccur:1; uint8_t ubFilterAlarmOccur:1; uint8_t ubCtrlErr:1;

    // DATA 4~16: 예약 영역 (송신 시 반드시 17바이트를 채워야 함)
    uint8_t reserved[13];
} HimpelControlMessage;

#pragma pack(pop)

/* --- [6] 주요 API 함수 프로토콜 --- */

// 초기화 및 상태 머신 관리
void Himpel_Init(void);
void Himpel_RestartRxIT(void);
void HimpelSmReset(void);
void HimpelSmInput(uint8_t input);
void Himpel_UART_ISR(UART_HandleTypeDef *huart);

// Setter API (제어 명령 전송용) — 0=RS485 성공, -1=실패/타임아웃
int setAPPower(int on);
int setAPMode(const char* mode);
int setAPSpeed(int speed);
int setAPUV(int on);
int setAPFilterReset(int on);
int setAPBypass(int on);
int setAPTimer(int minutes);
int  setAPAll(int pwr, const char* mode, int speed, int uv,
              int filter_reset, int bypass, int timer_minutes);
// Getter API (현재 수신된 상태 조회용)
int   getAPPower(void);
int   getAPSpeed(void);
int   getAPCo2(void);
int   getAPDust(void);
float getAPTemperature(int indoor); // 1: 실내, 0: 실외
void  getAPModeString(char* dest);
int   getFanStatus(void);
int   requestAPUpdate(void);

// 실제 패킷 전송 (RS485 인터페이스와 연결 필요)
int HimpelSendData(HIMPEL_ID da, HIMPEL_CMD cmd, const HimpelControlMessage* msg);

#endif /* INC_HIMPEL_H_ */
