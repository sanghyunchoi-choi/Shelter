#ifndef INC_APP_H_
#define INC_APP_H_

#include "main.h"
#include "config.h"
#include "cmsis_os2.h"
#include "MQTTClient.h"
//#include "updm010ub.h"  // 먼지 센서
#include "HCSD.h"       // 내부 온습도 센서
#include "mqtt_handler.h"
#include "himpel.h"     // 공기청정기/환기
#include "modbus_lg.h"  // 에어컨(LG)
#include <stdbool.h>
#include <string.h>

/* ======================================================================
   [1. 상수 정의]
   ====================================================================== */
#define MQTT_SOCKET_NUM       0
#define SNTP_SOCKET_NUM       1
#define DNS_SOCKET_NUM        2
#define MQTT_KEEP_ALIVE       60*5
#define MQTT_RECONNECT_DELAY  5000
#define TOPIC_SIZE            64
#define HEARTBEAT_INTERVAL    (10 * 60 * 1000) // 10분 주기 보고
#define MAX_POWER_CHANNELS    8
#define MAX_EXT_INPUT         4
#define MAX_SW_INPUT          8

#define PB_ID		5
/* ======================================================================
   [2. 데이터 구조체 정의]
   ====================================================================== */

// 입력 상태 (외부 입력 & 8단 스위치)
typedef struct {
    bool p4, p5, p6, p7;
    uint8_t sw_val;
} InputStatus;

// 전력 데이터
typedef struct {
	int	  b_id;
    int   ch;
    char  pwr[8];
    float curr;
    float watt;
    int   volt;
} PowerData;

// 센서 데이터
typedef struct {
    int   pm1_0, pm2_5, pm10;
    bool  is_connected;
} DustData;

typedef struct {
    float temp, humi;
    bool  is_connected;
} THData;

// 가전 데이터 (Himpel AP)
typedef struct {
    /* [1] 기본 제어 상태 */
    char  pwr[8];           // "ON", "OFF"
    char  mode[16];         // "AUTO", "CLEAN", "VENT", "BYPASS", "HEATER" 등
    int   fan_speed;        // 0(OFF) ~ 4(TURBO)
    bool  uv_on;            // UV 제균기 동작 여부

    /* [2] 정밀 공기질 데이터 (Air Quality) */
    int   co2;              // CO2 농도 (ppm)
    int   dust_pm25;        // 초미세먼지 (PM2.5)
    int   dust_pm10;        // 미세먼지 (PM10)
    int   dust_pm1_0;       // 극초미세먼지 (PM1.0)
    int   tvoc;             // 휘발성 유기화합물 농도

    /* [3] 온습도 데이터 (Environmental) */
    float temp_in;          // 실내 온도 (음수 대응)
    float temp_out;         // 실외 온도 (음수 대응)
    int   humi;             // 실내 습도 (%)

    /* [4] 장치 관리 및 연결 상태 */
    int   filter_life;      // 필터 잔량 (0~100%)
    bool  is_connected;     // RS485 통신 연결 여부
    uint8_t error_code;     // DATA 3 영역의 에러 비트 필드 저장용
} APData;


// 가전 데이터 (LG AC)
typedef struct {
    char  pwr[8], mode[16];
    float temp_sel;   // 희망 온도 (25.0)
    float temp_curr;  // 현재 온도 (24.5) - float으로 변경!
    int   fan_speed;
    bool  is_connected;
    int   error_code;
} ACData;

// 릴레이 및 자동문

typedef struct {
    bool ch[15];
} RelayData;

// 쿨러 팬
typedef struct {
    int   rpm, duty_percent;
    char  mode[12];
    bool is_connected;
} FanData;

/* ======================================================================
   [3. 통합 장치 상태 (Global Snapshot)]
   ====================================================================== */
typedef struct {
    char        uid[SHELTER_DEVICE_UID_LEN];
    DustData    dust;
    THData      th_in, th_out;
    RelayData   relay;
    PowerData   pwr_ch[MAX_POWER_CHANNELS];
    bool        is_pwr_connected;
    APData      ap;
    ACData      ac;
    FanData     fan;
    InputStatus in_stat;
} DeviceStatus;

/* ======================================================================
   [4. MQTT 토픽 관리 구조체 (규격 일치)]
   ====================================================================== */
typedef struct {
    // 1. System & Sensors
    char tele_state[TOPIC_SIZE], tele_dust[TOPIC_SIZE];
    char tele_th_in[TOPIC_SIZE], tele_th_out[TOPIC_SIZE];
    char cmnd_dev[TOPIC_SIZE], stat_dev[TOPIC_SIZE], cmnd_all[TOPIC_SIZE];

    // 2. Cooler Fan (내부 팬)
    char tele_fan[TOPIC_SIZE];
    char cmnd_fan_set[TOPIC_SIZE], cmnd_fan_get[TOPIC_SIZE];
    char stat_fan[TOPIC_SIZE];

    // 3. Relay
    char tele_ad[TOPIC_SIZE];        // 상태 보고 (dev/tele/ad)
    char cmnd_ad_get[TOPIC_SIZE];    // 상태 조회 (get_ad)
    char stat_ad[TOPIC_SIZE];        // 제어 응답 (dev/stat/ad)

    // 4. Himpel AP (환기청정기)
    char tele_ap[TOPIC_SIZE];
    char cmnd_ap_pwr[TOPIC_SIZE], cmnd_ap_mod[TOPIC_SIZE];
    char cmnd_ap_spd[TOPIC_SIZE], cmnd_ap_uv[TOPIC_SIZE];
    char cmnd_ap_setall[TOPIC_SIZE], cmnd_ap_getall[TOPIC_SIZE];
    char stat_ap[TOPIC_SIZE];

    // 5. Power Board (전원보드 - pb)
    char tele_pb[TOPIC_SIZE];        // 이미지상 pb로 명시됨
    char cmnd_pb_ch[TOPIC_SIZE];     // 개별 채널 제어 (set_pb: ch, sw)
    char cmnd_pb_all[TOPIC_SIZE];    // 전체 채널 제어 (set_pb: sw 문자열)
    char cmnd_pb_get[TOPIC_SIZE];    // 상태 조회 (get_pb)
    char stat_pb[TOPIC_SIZE];

    // 6. LG AC (에어컨)
    char tele_ac[TOPIC_SIZE];
    char cmnd_ac_pwr[TOPIC_SIZE], cmnd_ac_mod[TOPIC_SIZE];
    char cmnd_ac_temp[TOPIC_SIZE], cmnd_ac_spd[TOPIC_SIZE];
    char cmnd_ac_setall[TOPIC_SIZE], cmnd_ac_getall[TOPIC_SIZE];
    char stat_ac[TOPIC_SIZE];
} MQTT_Topics;


#if 0
// EEPROM 저장용 통합 설정 구조체 (Config)
typedef struct {
    uint32_t magic;             // 0x55AA1234 (데이터 유효성 검증용)

    /* 1. 네트워크 및 식별 정보 (Config.txt 역할) */
    char     ip_addr[16];       // 고정 IP (예: "192.168.1.100")
    uint16_t port;              // MQTT 포트 (예: 1883)
    char     uuid[32];          // 보드 고유 ID (부팅 후 저장 가능)

    /* 2. 에어컨(AC) 설정 정보 */
    int      ac_temp_last;      // 마지막 설정 온도
    uint8_t  ac_mode_last;      // 마지막 모드 (0:COOL, 1:DRY 등)
    int      ac_speed_last;     // 마지막 풍량

    /* 3. 공기청정기(AP) 설정 정보 */
    uint8_t  ap_mode_last;      // 0:Auto, 1:Manual
    int      ap_speed_last;     // 팬 속도
    bool     ap_uv_on;          // UV 살균등 상태

    /* 4. 시스템 보호 및 검증 */
    uint16_t checksum;          // 데이터 무결성 체크 (전체 바이트 합산)
} ShelterConfig;

extern ShelterConfig sys_cfg;   // 전역 설정 변수
#endif
typedef struct { GPIO_TypeDef* port; uint16_t pin; } RelayPinMap;

/* ======================================================================
   [5. 전역 변수 Extern 선언 (Task & Handler 공유)]
   ====================================================================== */
// A. 핵심 통신 및 상태 객체
extern MQTT_Topics      topics;
extern MQTTClient       c;
extern DeviceStatus     dev_status;
extern osMutexId_t      mqtt_mutex_id;
extern volatile bool    is_mqtt_connected;
extern volatile float   fan_rpm;                // 실시간 RPM 참조용

// B. 보고 제어용 플래그 (volatile: 태스크 간 최적화 방지)
extern volatile bool    pub_done_pwr[MAX_POWER_CHANNELS];
extern volatile bool    pub_done_pwr_all;       // 통합 전원 보고용 추가
extern volatile bool    pub_done_ap, pub_done_ac, pub_done_fan;
extern volatile bool    pub_done_relay, pub_done_dust;
extern volatile bool    pub_done_th_in, pub_done_th_out;

// C. 보고 주기 관리용 타이머 (하트비트 리셋용)
extern uint32_t         last_pwr_all_pub_tick;
extern uint32_t         last_ap_pub_tick, last_ac_pub_tick, last_fan_pub_tick;
extern uint32_t         last_dust_pub_tick, last_state_pub_tick; // 통합 상태용 추가
extern uint32_t         last_th_in_pub_tick, last_th_out_pub_tick;

// D. 상태 백업 변수 (중복 보고 및 Changed 판정 방지용)
extern PowerData        last_sent_pwr[MAX_POWER_CHANNELS];
extern ACData           last_sent_ac;
extern APData           last_sent_ap;
extern bool             last_sent_relay[15];     // 릴레이 백업 추가
extern int              last_sent_fan_duty;
extern char             last_sent_fan_mode[12];
extern char             last_sent_fan_health[12];

/* ======================================================================
   [6. 함수 프로토타입]
   ====================================================================== */
void MX_App_Init(void);
bool Sync_RTC_With_NTP(void);

#endif /* INC_APP_H_ */
