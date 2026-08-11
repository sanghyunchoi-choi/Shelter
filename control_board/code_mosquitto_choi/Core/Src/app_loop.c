#include "app.h"
#include <ds3231m.h> // _RTC 타입을 위해 포함
#include "sntp.h"
#include "dns.h"
#include "socket.h"
#include <stdio.h>

#define DEFAULT_DOOR_TIME    10000  // 기본 10초 (ms 단위)

// 전역 또는 상단에 선언될 이전 핀 상태 비교용 변수 (로그 출력 제어용)
static bool last_sw1 = false, last_sw2 = false, last_sw3 = false, last_sw4 = false;
static bool last_sw5 = false, last_sw6 = false, last_sw7 = false, last_sw8 = false;


/* --- 외부 전역 변수 참조 --- */
extern unsigned long MilliTimer;

/* --- NTP/DNS용 내부 버퍼 --- */
static uint8_t g_dns_buf[MAX_DNS_BUF_SIZE];
static uint8_t g_sntp_buf[1024];

// 1번~15번 릴레이 물리 핀 매핑 (main.h의 정의 사용)
const RelayPinMap RLY_MAP[15] = {
		{rly_1_GPIO_Port, rly_1_Pin},   {rly_2_GPIO_Port, rly_2_Pin},
		{rly_3_GPIO_Port, rly_3_Pin},   {rly_4_GPIO_Port, rly_4_Pin},
		{rly_5_GPIO_Port, rly_5_Pin},   {rly_6_GPIO_Port, rly_6_Pin},
		{rly_7_GPIO_Port, rly_7_Pin},   {rly_8_GPIO_Port, rly_8_Pin},
		{rly_9_GPIO_Port, rly_9_Pin},   {rly_10_GPIO_Port, rly_10_Pin},
		{rly_11_GPIO_Port, rly_11_Pin}, {rly_12_GPIO_Port, rly_12_Pin},
		{rly_13_GPIO_Port, rly_13_Pin}, {rly_14_GPIO_Port, rly_14_Pin},
		{rly_15_GPIO_Port, rly_15_Pin}
};

/**
 * @brief NTP 서버로부터 시간을 수신하여 RTC에 설정
 */
bool Sync_RTC_With_NTP(void) {
	uint8_t dns_server_ip[4] = {8, 8, 8, 8};
	uint8_t ntp_domain[] = "time.google.com";
	uint8_t target_ntp_ip[4];
	datetime srv_time;
	int8_t res = 0;

	// 1. DNS Query
	DNS_init(DNS_SOCKET_NUM, g_dns_buf);
	if (DNS_run(dns_server_ip, ntp_domain, target_ntp_ip) != 1) return false;

	// 2. SNTP 실행
	close(SNTP_SOCKET_NUM);
	if (socket(SNTP_SOCKET_NUM, Sn_MR_UDP, 12345, 0) != SNTP_SOCKET_NUM) return false;

	SNTP_init(SNTP_SOCKET_NUM, target_ntp_ip, 40, g_sntp_buf);

	for(int retry = 0; retry < 3; retry++) {
		MilliTimer = HAL_GetTick(); // ioLibrary 타임아웃 갱신
		res = SNTP_run(&srv_time);
		if (res == 1) break;
		osDelay(1000);
	}

	// 3. RTC 설정 (_RTC 타입 에러 해결)
	if (res == 1) {
		_RTC new_rtc = {
				.Year = (uint8_t)(srv_time.yy - 2000),
				.Month = (uint8_t)srv_time.mo,
				.Date = (uint8_t)srv_time.dd,
				.Hour = (uint8_t)srv_time.hh,
				.Min = (uint8_t)srv_time.mm,
				.Sec = (uint8_t)srv_time.ss,
				.DaysOfWeek = 1
		};
		return DS3231_SetTime(&new_rtc);
	}
	return false;
}


/* 장치별 온라인 여부 확인 함수 */

// 1. 전원 보드 (통합)
bool Is_PowerBoard_Online(void) {
	bool online;
	osMutexAcquire(mqtt_mutex_id, osWaitForever);
	online = dev_status.is_pwr_connected;
	osMutexRelease(mqtt_mutex_id);
	return online;
}

// 2. 공기청정기 (Himpel)
bool Is_AP_Online(void) {
	bool online;
	osMutexAcquire(mqtt_mutex_id, osWaitForever);
	online = dev_status.ap.is_connected;
	osMutexRelease(mqtt_mutex_id);
	return online;
}

// 3. 에어컨 (LG)
bool Is_AC_Online(void) {
	bool online;
	osMutexAcquire(mqtt_mutex_id, osWaitForever);
	online = dev_status.ac.is_connected;
	osMutexRelease(mqtt_mutex_id);
	return online;
}

// 4. 환경 센서 (Dust/TH)
bool Is_Sensor_Online(DustData *dust, THData *th) {
	// 특정 센서 그룹의 연결 상태를 한 번에 확인하고 싶을 때 사용
	return (dev_status.dust.is_connected && dev_status.th_in.is_connected);
}
void RLY_SetStatus(uint8_t ch, bool state) {
	if (ch < 1 || ch > 15) return;

	// 1. 물리적 핀 제어 (High Active 기준, 회로에 따라 RESET/SET 반전 가능)
	HAL_GPIO_WritePin(RLY_MAP[ch-1].port, RLY_MAP[ch-1].pin,
			state ? GPIO_PIN_SET : GPIO_PIN_RESET);

	// 2. [중요] 전역 상태 구조체 업데이트
	// 여기서 업데이트된 값이 10분 주기 보고 시 relays 배열로 나갑니다.
	osMutexAcquire(mqtt_mutex_id, osWaitForever);
	dev_status.relay.ch[ch-1] = state;
	osMutexRelease(mqtt_mutex_id);

	//printf("[RLY] Channel %d set to %s\r\n", ch, state ? "ON" : "OFF");
}

// 릴레이 개별 상태 읽기 (ch: 1~15)
bool RLY_GetStatus(uint8_t ch) {
	if (ch < 1 || ch > 15) return false;
	return (HAL_GPIO_ReadPin(RLY_MAP[ch-1].port, RLY_MAP[ch-1].pin) == GPIO_PIN_SET);
}
void RLY_All_Reset(void) {
	osMutexAcquire(mqtt_mutex_id, osWaitForever);
	for (int i = 1; i <= 15; i++) {
		// 기존 상태 읽기 (이미 꺼져있는지 하드웨어 상태 체크)
		// RLY_GetStatus는 현재 핀이 SET(ON) 상태이면 true를 반환함
		if (RLY_GetStatus(i) == true) {
			// 1. 물리 핀 끄기 (1번부터 순서대로 진행)
			HAL_GPIO_WritePin(RLY_MAP[i-1].port, RLY_MAP[i-1].pin, GPIO_PIN_RESET);

			// 2. 전역 데이터 구조체 업데이트
			osMutexAcquire(mqtt_mutex_id, osWaitForever);
			dev_status.relay.ch[i-1] = false;
			osMutexRelease(mqtt_mutex_id);

			//printf("[RLY] Channel %d -> Turned OFF\r\n", i);

			// 보드 전원 부담을 줄이기 위해 제어 발생 시에만 200ms 지연
			osDelay(200);
		} else {
			// 이미 꺼져있다면 제어 및 딜레이 없이 즉시 패스
			// printf("[RLY] Channel %d -> Already OFF (Skip)\r\n", i); // 디버그 필요시 주석 해제
		}
	}
	osMutexRelease(mqtt_mutex_id);
	printf("[RLY] All 15 Channels Reset (OFF).\r\n");
}

// 개별 이전 상태 저장 변수 (초기값은 알 수 없음으로 설정)
static int8_t last_p4 = -1, last_p5 = -1, last_p6 = -1, last_p7 = -1;
static uint8_t last_sw_val = 0xFF;

/**
 * @brief 외부 입력 4채널 개별 스캔 및 상태별 로그 출력
 */
// 최초 입력 부분이 low이다. high상태(외부 쇼트)에서 사용을 해야 된다.
/**
 * @brief 외부 입력 4채널 스캔 (디버깅용 로그만 남기고 상태는 상시 업데이트)
 */
void SCAN_External_Inputs(void) {
	// 실시간 핀 상태 읽기
	bool cp4 = (HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_4) == GPIO_PIN_SET);
	bool cp5 = (HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_5) == GPIO_PIN_SET);
	bool cp6 = (HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_6) == GPIO_PIN_SET);
	bool cp7 = (HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_7) == GPIO_PIN_SET);

	// [v2.0] 상승엣지(0→1, 신호 없음→있음) 발생 시 채널별 누적 카운트 증가.
	// 웹서버 화면의 "동그라미 아래 count" 숫자가 여기서 올라간 값입니다.
	if (cp4 && !dev_status.in_stat.p4) dev_status.in_stat.cnt4++;
	if (cp5 && !dev_status.in_stat.p5) dev_status.in_stat.cnt5++;
	if (cp6 && !dev_status.in_stat.p6) dev_status.in_stat.cnt6++;
	if (cp7 && !dev_status.in_stat.p7) dev_status.in_stat.cnt7++;

	// 상태가 바뀌면(신호 발생 또는 해제) 즉시 MQTT 보고 트리거 (10분 대기하지 않음)
	if (cp4 != dev_status.in_stat.p4 || cp5 != dev_status.in_stat.p5 ||
	    cp6 != dev_status.in_stat.p6 || cp7 != dev_status.in_stat.p7) {
		pub_done_input = false;
	}

	// 전역 구조체 업데이트 (MQTT Task에서 참조함)
	dev_status.in_stat.p4 = cp4;
	dev_status.in_stat.p5 = cp5;
	dev_status.in_stat.p6 = cp6;
	dev_status.in_stat.p7 = cp7;

	// 로그는 변화가 있을 때만 출력하여 터미널 과부하 방지
	if (cp4 != last_p4 || cp5 != last_p5 || cp6 != last_p6 || cp7 != last_p7) {
		printf("[INPUT] PD4-7 State Updated: %d %d %d %d (cnt %lu %lu %lu %lu)\r\n",
				cp4, cp5, cp6, cp7,
				(unsigned long)dev_status.in_stat.cnt4, (unsigned long)dev_status.in_stat.cnt5,
				(unsigned long)dev_status.in_stat.cnt6, (unsigned long)dev_status.in_stat.cnt7);
		last_p4 = cp4; last_p5 = cp5; last_p6 = cp6; last_p7 = cp7;
	}
}


void SCAN_External_Inputs_switch8(void) {
	// 1. [Active Low 정석 판독] 핀 레벨이 RESET(0V)일 때가 누름(true/1)입니다.
	bool c_sw1 = (HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_8)  == GPIO_PIN_RESET);
	bool c_sw2 = (HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_9)  == GPIO_PIN_RESET); // 깔끔하게 한 줄로 교정 완료!
	bool c_sw3 = (HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_10) == GPIO_PIN_RESET);
	bool c_sw4 = (HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_11) == GPIO_PIN_RESET);
	bool c_sw5 = (HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_12) == GPIO_PIN_RESET);
	bool c_sw6 = (HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_13) == GPIO_PIN_RESET);
	bool c_sw7 = (HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_14) == GPIO_PIN_RESET);
	bool c_sw8 = (HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_15) == GPIO_PIN_RESET);

	// 2. 전역 구조체 sw1 ~ sw8 변수명에 실시간 상태 업데이트
	dev_status.in_stat.sw1 = c_sw1;
	dev_status.in_stat.sw2 = c_sw2;
	dev_status.in_stat.sw3 = c_sw3;
	dev_status.in_stat.sw4 = c_sw4;
	dev_status.in_stat.sw5 = c_sw5;
	dev_status.in_stat.sw6 = c_sw6;
	dev_status.in_stat.sw7 = c_sw7;
	dev_status.in_stat.sw8 = c_sw8;

	// 3. 변이 감시 및 시리얼 로그 출력
	if (c_sw1 != last_sw1) { printf("[SWITCH] SW1 (PD8) State Changed ➔ %s\r\n", c_sw1 ? "ON" : "OFF"); last_sw1 = c_sw1; }
	if (c_sw2 != last_sw2) { printf("[SWITCH] SW2 (PD9) State Changed ➔ %s\r\n", c_sw2 ? "ON" : "OFF"); last_sw2 = c_sw2; }
	if (c_sw3 != last_sw3) { printf("[SWITCH] SW3 (PD10) State Changed ➔ %s\r\n", c_sw3 ? "ON" : "OFF"); last_sw3 = c_sw3; }
	if (c_sw4 != last_sw4) { printf("[SWITCH] SW4 (PD11) State Changed ➔ %s\r\n", c_sw4 ? "ON" : "OFF"); last_sw4 = c_sw4; }
	if (c_sw5 != last_sw5) { printf("[SWITCH] SW5 (PD12) State Changed ➔ %s\r\n", c_sw5 ? "ON" : "OFF"); last_sw5 = c_sw5; }
	if (c_sw6 != last_sw6) { printf("[SWITCH] SW6 (PD13) State Changed ➔ %s\r\n", c_sw6 ? "ON" : "OFF"); last_sw6 = c_sw6; }
	if (c_sw7 != last_sw7) { printf("[SWITCH] SW7 (PD14) State Changed ➔ %s\r\n", c_sw7 ? "ON" : "OFF"); last_sw7 = c_sw7; }

	if (c_sw8 != last_sw8) {
		printf("[SWITCH] SW8 (PD15) State Changed ➔ %s\r\n", c_sw8 ? "ON" : "OFF");

		/*
		 * [★무한 리셋 절대 방어 가드]
		 * 이전 루프에서 분명히 꺼져있다가(last_sw8 == false)
		 * 이번 루프에서 처음 딱 켜지는(c_sw8 == true) '상승 엣지 순간'에만 단 1번 실행!
		 * 버튼을 계속 누른 채로 리셋되어 다시 켜져도 조건문이 차단되어 무한 리셋에 절대 안 빠짐.
		 */
		if (c_sw8 && !last_sw8) {
			printf("[FACTORY-RESET] SW8 하드웨어 복구 버튼 감지! 설정을 DHCP 모드로 밀고 재부팅합니다.\r\n");

			// 램 상에서 네트워크 모드를 DHCP(1)로 원상 복구 스위칭
			NetRuntimeConfig reset_cfg = g_net_cfg;
			reset_cfg.net_mode = 1;

			// 비교 변수를 미리 세팅하여 이중 트리거 예방 처리 후 내장 플래시 영구 각인 및 재부팅 위임
			last_sw8 = c_sw8;
			NetConfig_SaveAndApply(&reset_cfg);
			return;
		}

		last_sw8 = c_sw8;
	}
}



/**
 * @brief 8단 스위치 조합 스캔 (누적 합산 방식)
 */
void SCAN_Switch_Configuration(void) {
	uint8_t raw_val = 0;
	for (int i = 0; i < 8; i++) {
		if (HAL_GPIO_ReadPin(GPIOD, (GPIO_PIN_8 << i)) == GPIO_PIN_SET) {
			raw_val |= (1 << i);
		}
	}

	// Pull-up 대응 비트 반전 (안 누르면 0)
	uint8_t current_sw_val = ~raw_val;

	if (current_sw_val != last_sw_val) {
		printf("[INPUT] SW Config Changed: %d\r\n", current_sw_val);
		dev_status.in_stat.sw_val = current_sw_val;
		last_sw_val = current_sw_val;
	}
}
