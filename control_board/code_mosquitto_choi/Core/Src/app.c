#include "app.h"
#include "app_loop.h"
#include "config.h"
#include <w5500_ctrl.h>
#include <ds3231m.h>
#include "sntp.h"
#include "dns.h"
#include "MQTTClient.h"
#include "mqtt_interface.h"
#include "mqtt_handler.h"
#include "wizchip_conf.h"
#include "socket.h"
#include "HCSD.h"
#include "cwt_th03s.h"
#include "power_board.h"
#include "himpel.h"
#include "updm010ub.h"
#include "25LC256.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ======================================================================
 [보고 주기 및 타임아웃 설정]
   - PERIODIC_REPORT_MS : 센서/장치 주기 보고 간격
   - DUST_TIMEOUT       : 먼지 센서 무응답 판정 기준 (ms)
   - FAN_TEMP_MIN/MAX   : 팬 AUTO 모드 동작 온도 범위
 ====================================================================== */
#define PERIODIC_REPORT_MS  600000   // 10분 (10 * 60 * 1000)
#define POWER_REPORT_MS     10000    // 전원 보드 보고 주기 (10초)
#define DUST_TIMEOUT          5000   // 5초 무응답 → 연결 끊김 판정
#define FAN_TEMP_MIN           0.0f  // 팬 정지 온도 (0%)
#define FAN_TEMP_MAX          20.0f  // 팬 최대 가동 온도 (100%)
#define FAN_TEMP_RANGE  (FAN_TEMP_MAX - FAN_TEMP_MIN)

extern void getAPStatusAll(APData* dest);

/* ======================================================================
 [1. 통합 장치 상태 관리 (Global Status Snapshot)]
 ====================================================================== */
DeviceStatus dev_status = {
		.uid = "",
		.dust    = { .pm1_0 = 0, .pm2_5 = 0, .pm10 = 0, .is_connected = false },
		.th_in   = { .temp = 0.0f, .humi = 0.0f, .is_connected = false },
		.th_out  = { .temp = 0.0f, .humi = 0.0f, .is_connected = false },
		.fan     = { .rpm = 0, .duty_percent = 50, .mode = "AUTO", .is_connected = false },
		.pwr_ch  = {
				{1,PB_ID,"OFF",0.0f,0.0f,220}, {2,PB_ID,"OFF",0.0f,0.0f,220},
				{3,PB_ID,"OFF",0.0f,0.0f,220}, {4,PB_ID,"OFF",0.0f,0.0f,220},
				{5,PB_ID,"OFF",0.0f,0.0f,220}, {6,PB_ID,"OFF",0.0f,0.0f,220},
				{7,PB_ID,"OFF",0.0f,0.0f,220}, {8,PB_ID,"OFF",0.0f,0.0f,220},
		},
		.is_pwr_connected = false,
		.relay   = { .ch = { false, } },
		.in_stat = { .p4=false,.p5=false,.p6=false,.p7=false,.sw_val=0 },
		.ap      = { .pwr="OFF",.mode="AUTO",.fan_speed=0,.filter_life=100,.uv_on=false,.is_connected=false },
		.ac      = { .pwr="OFF",.mode="COOL",.temp_sel=24,.temp_curr=0,.fan_speed=1,.is_connected=false }
};

/* ======================================================================
 [2. 시스템 및 MQTT 통신 객체]
 ====================================================================== */
_RTC           current_rtc;
MQTTClient     c;
MQTT_Topics    topics;
osMutexId_t    mqtt_mutex_id;

/* W5500 하드웨어 + MQTT 라이브러리 공유 자원 보호용 Mutex.
   없으면 간헐적 HardFault / MQTT Disconnect 발생 가능. */
volatile bool  is_mqtt_connected = false;
volatile bool  is_time_synced    = false;
extern unsigned long MilliTimer;

/* ======================================================================
 [3. 보고 상태 플래그 및 타이머]
 ====================================================================== */
volatile bool  pub_done_pwr[MAX_POWER_CHANNELS] = {false};
volatile bool  pub_done_pwr_all  = false;
uint32_t       last_pwr_all_pub_tick = 0;
uint32_t       last_pwr_recv_tick    = 0;

volatile bool  pub_done_ap    = false;
volatile bool  pub_done_ac    = false;
volatile bool  pub_done_fan   = false;
volatile bool  pub_done_relay = false;
volatile bool  pub_done_dust  = false;
volatile bool  pub_done_th_in = false;
volatile bool  pub_done_th_out= false;

uint32_t       last_state_pub_tick = 0;
uint32_t       last_ap_pub_tick    = 0;
uint32_t       last_ac_pub_tick    = 0;
uint32_t       last_fan_pub_tick   = 0;
uint32_t       last_dust_pub_tick  = 0;
uint32_t       last_th_in_pub_tick = 0;
uint32_t       last_th_out_pub_tick= 0;

/* ======================================================================
 [4. 데이터 백업 변수 (mqtt_handler 동기화용)]
 ====================================================================== */
PowerData  last_sent_pwr[MAX_POWER_CHANNELS];
APData     last_sent_ap;
ACData     last_sent_ac;
DustData   last_sent_dust;
THData     last_sent_th_in, last_sent_th_out;
FanData    last_sent_fan;
int        last_sent_fan_duty = -1;
char       last_sent_fan_mode[12]   = "";
char       last_sent_fan_health[12] = "";
bool       last_sent_relay[15]      = {false};

/* ======================================================================
 [5. 하드웨어 진단 및 네트워크 관련]
 ====================================================================== */
uint32_t       last_signal_tick    = 0;
uint32_t       last_broken_tick    = 0;
int            broken_report_cnt   = 0;
volatile float fan_rpm             = 0;
uint8_t        boot_diag_delay     = 5;
uint32_t       last_dust_recv_tick = 0;
uint32_t       last_in_recv_tick   = 0;
uint32_t       last_out_recv_tick  = 0;
uint32_t       last_ap_recv_tick   = 0;
uint32_t       last_ac_recv_tick   = 0;
uint32_t       disconnect_tick     = 0;
bool           show_net_error_log  = true;

/* ======================================================================
 [MQTT_Reset_Report_Flags]
 재연결 시 모든 도메인 데이터를 서버와 즉시 동기화하기 위해
 플래그·타이머를 일괄 리셋합니다.
 ====================================================================== */
void MQTT_Reset_Report_Flags(void)
{
	uint32_t now = HAL_GetTick();

	/* 1. 전원 보드 */
	pub_done_pwr_all      = false;
	last_pwr_all_pub_tick = now - HEARTBEAT_INTERVAL;
	for (int i = 0; i < MAX_POWER_CHANNELS; i++) {
		pub_done_pwr[i]          = false;
		last_sent_pwr[i].watt    = -1.0f;
	}

	/* 2. 가전(AP, AC) 및 팬 */
	pub_done_ap = false;  last_sent_ap.fan_speed = -1;   last_ap_pub_tick  = now - HEARTBEAT_INTERVAL;
	pub_done_ac = false;  last_sent_ac.temp_sel  = -1;   last_ac_pub_tick  = now - HEARTBEAT_INTERVAL;
	pub_done_fan= false;  last_sent_fan_duty     = -1;   last_fan_pub_tick = now - HEARTBEAT_INTERVAL;

	/* 3. 환경 센서 */
	pub_done_dust  = false;  last_sent_dust.pm2_5   = -1;      last_dust_pub_tick   = now - HEARTBEAT_INTERVAL;
	pub_done_th_in = false;  last_sent_th_in.temp   = -99.0f;  last_th_in_pub_tick  = now - HEARTBEAT_INTERVAL;
	pub_done_th_out= false;  last_sent_th_out.temp  = -99.0f;  last_th_out_pub_tick = now - HEARTBEAT_INTERVAL;

	/* 4. 릴레이 및 시스템 */
	pub_done_relay   = false;
	last_state_pub_tick = now - HEARTBEAT_INTERVAL;
	for (int i = 0; i < 15; i++)
		last_sent_relay[i] = !dev_status.relay.ch[i]; // 반대값 → 변화 강제 감지

	printf("[MQTT] All Report Flags & Heartbeat Timers Reset. Full Sync Triggered.\r\n");
}

/* ======================================================================
 [StartMQTTTask] — MQTT 연결 관리 및 세션 유지
 ====================================================================== */
void StartMQTTTask(void *argument)
{
	Network n;
	uint8_t buf[512], readbuf[512];
	char    uuid[SHELTER_DEVICE_UID_LEN], payload[128];

	Board_GetDeviceUuid(uuid, sizeof(uuid));
	strncpy(dev_status.uid, uuid, sizeof(dev_status.uid) - 1);
	dev_status.uid[sizeof(dev_status.uid) - 1] = '\0';
	printf("[BOARD] Device UID: %s\r\n", dev_status.uid);

	/* --- 2. 토픽 경로 초기화 --- */
	/* [System & Sensors] */
	snprintf(topics.tele_state,  TOPIC_SIZE, "dev/tele/state");
	snprintf(topics.tele_dust,   TOPIC_SIZE, "dev/tele/dust");
	snprintf(topics.tele_th_in,  TOPIC_SIZE, "dev/tele/th_in");
	snprintf(topics.tele_th_out, TOPIC_SIZE, "dev/tele/th_out");
	snprintf(topics.cmnd_dev,    TOPIC_SIZE, "dev/cmnd/dev");
	snprintf(topics.stat_dev,    TOPIC_SIZE, "dev/stat/dev");
	snprintf(topics.cmnd_all,    TOPIC_SIZE, "dev/cmnd/#");
	/* [Cooler Fan] */
	snprintf(topics.tele_fan,     TOPIC_SIZE, "dev/tele/fan");
	snprintf(topics.cmnd_fan_set, TOPIC_SIZE, "dev/cmnd/fan/duty");
	snprintf(topics.cmnd_fan_get, TOPIC_SIZE, "dev/cmnd/fan/get");
	snprintf(topics.stat_fan,     TOPIC_SIZE, "dev/stat/fan");
	/* [Relay] */
	snprintf(topics.tele_ad,     TOPIC_SIZE, "dev/tele/ad");
	snprintf(topics.cmnd_ad_get, TOPIC_SIZE, "dev/cmnd/ad/get");
	snprintf(topics.stat_ad,     TOPIC_SIZE, "dev/stat/ad");
	/* [Himpel AP] */
	snprintf(topics.tele_ap,       TOPIC_SIZE, "dev/tele/ap");
	snprintf(topics.cmnd_ap_pwr,   TOPIC_SIZE, "dev/cmnd/ap/power");
	snprintf(topics.cmnd_ap_mod,   TOPIC_SIZE, "dev/cmnd/ap/mode");
	snprintf(topics.cmnd_ap_spd,   TOPIC_SIZE, "dev/cmnd/ap/speed");
	snprintf(topics.cmnd_ap_uv,    TOPIC_SIZE, "dev/cmnd/ap/uv");
	snprintf(topics.cmnd_ap_setall,TOPIC_SIZE, "dev/cmnd/ap/set_all");
	snprintf(topics.cmnd_ap_getall,TOPIC_SIZE, "dev/cmnd/ap/get");
	snprintf(topics.stat_ap,       TOPIC_SIZE, "dev/stat/ap");
	/* [Power Board] */
	snprintf(topics.tele_pb,    TOPIC_SIZE, "dev/tele/pb");
	snprintf(topics.cmnd_pb_ch, TOPIC_SIZE, "dev/cmnd/pb/ch");
	snprintf(topics.cmnd_pb_all,TOPIC_SIZE, "dev/cmnd/pb/all");
	snprintf(topics.cmnd_pb_get,TOPIC_SIZE, "dev/cmnd/pb/get");
	snprintf(topics.stat_pb,    TOPIC_SIZE, "dev/stat/pb");
	/* [LG AC] */
	snprintf(topics.tele_ac,       TOPIC_SIZE, "dev/tele/ac");
	snprintf(topics.cmnd_ac_pwr,   TOPIC_SIZE, "dev/cmnd/ac/power");
	snprintf(topics.cmnd_ac_mod,   TOPIC_SIZE, "dev/cmnd/ac/mode");
	snprintf(topics.cmnd_ac_temp,  TOPIC_SIZE, "dev/cmnd/ac/temp");
	snprintf(topics.cmnd_ac_spd,   TOPIC_SIZE, "dev/cmnd/ac/speed");
	snprintf(topics.cmnd_ac_setall,TOPIC_SIZE, "dev/cmnd/ac/set_all");
	snprintf(topics.cmnd_ac_getall,TOPIC_SIZE, "dev/cmnd/ac/get");
	snprintf(topics.stat_ac,       TOPIC_SIZE, "dev/stat/ac");

	/* --- 3. Mutex 생성 --- */
	const osMutexAttr_t mqtt_mutex_attr = { .name="mqtt_mutex", .attr_bits=osMutexRecursive };
	mqtt_mutex_id = osMutexNew(&mqtt_mutex_attr);
	if (mqtt_mutex_id == NULL) printf("[ERR] Mutex Creation Failed!\r\n");

	/* 시간 동기화 대기 */
	while (!is_time_synced) osDelay(500);

	/* W5500 재전송 파라미터 */
	setRTR(2000);   // 재전송 타이머 200ms
	setRCR(3);      // 재전송 횟수 3회

	/* ================================================================
       MQTT 메인 루프 : 연결 관리 및 세션 유지
    ================================================================ */

	/* NTP는 최초 1회 + 이후 24시간 간격으로만 동기화 */
	static uint32_t last_ntp_sync_tick = 0;
#define NTP_SYNC_INTERVAL_MS  (24UL * 60UL * 60UL * 1000UL)  // 24시간


	for (;;) {
		is_mqtt_connected = false;
		uint8_t ip[4];

		/* [Step 1] 물리 소켓 완전 초기화 (Zombie Connection 방지) */
		if (getSn_SR(MQTT_SOCKET_NUM) != SOCK_CLOSED) {
			disconnect(MQTT_SOCKET_NUM);
			setSn_CR(MQTT_SOCKET_NUM, Sn_CR_CLOSE);
			while (getSn_CR(MQTT_SOCKET_NUM));
			uint32_t wait_tick = HAL_GetTick();
			while (getSn_SR(MQTT_SOCKET_NUM) != SOCK_CLOSED &&
					(HAL_GetTick() - wait_tick < 200)) {
				osDelay(1);
			}
		}
		osDelay(200);

		/* [Step 2] 링크 + 유효 IPv4 대기 (config.h: STATIC / DHCP) */
		{
			bool link_was_lost = false;
			uint32_t wait_log_tick = 0;

			while (!W5500_NetworkReady(ip)) {
				if (!link_was_lost) {
					osDelay(1000);
					if (W5500_NetworkReady(ip)) {
						break;
					}
					link_was_lost = true;
					if (disconnect_tick == 0) {
						disconnect_tick = HAL_GetTick();
					}
					wait_log_tick = HAL_GetTick();
					if ((getPHYCFGR() & 0x01) == 0) {
						printf("[WAIT] Ethernet cable down\r\n");
					} else {
						printf("[WAIT] Waiting for network (see config.h STATIC/DHCP)...\r\n");
					}
				} else {
					if (HAL_GetTick() - wait_log_tick >= 10000) {
						wait_log_tick = HAL_GetTick();
						printf("[WAIT] Network not ready (%lu s)\r\n",
								(unsigned long)((HAL_GetTick() - disconnect_tick) / 1000));
					}
					osDelay(1000);
				}
			}
			if (link_was_lost) {
				printf("[NET] Link up. LAN %d.%d.%d.%d\r\n",
						ip[0], ip[1], ip[2], ip[3]);
			}
		}

		/* [Step 3] NTP 시간 동기화
           ★ 수정: 매 재연결마다 호출하지 않고,
             - 최초 부팅 1회 (last_ntp_sync_tick == 0)
             - 이후 24시간 경과 시에만 재동기화합니다.
             MQTT가 짧은 주기로 재연결될 때 NTP 쿼리가 폭주하는
             문제를 방지합니다.                                    */
		bool do_ntp = (last_ntp_sync_tick == 0) ||
				(HAL_GetTick() - last_ntp_sync_tick >= NTP_SYNC_INTERVAL_MS);
		if (do_ntp) {
			printf("[NTP] Syncing time...\r\n");
			if (W5500_Sync_RTC_From_NTP() == 1) {
				last_ntp_sync_tick = HAL_GetTick();
				Log_With_Time("SYS", "NTP Sync Done.");
			} else {
				Log_With_Time("WARN", "NTP Sync Failed. Using backup time.");
				/* 실패 시 재시도 간격을 짧게 유지 (last_ntp_sync_tick 갱신 안 함) */
			}
		}

		/* [Step 4] TCP 레이어 연결 */
		uint8_t broker_ip[] = SHELTER_MQTT_BROKER_IP;
		printf("[MQTT] Connecting to %u.%u.%u.%u:%d...\r\n",
				broker_ip[0], broker_ip[1], broker_ip[2], broker_ip[3],
				SHELTER_MQTT_BROKER_PORT);
		NewNetwork(&n, MQTT_SOCKET_NUM);
		if (ConnectNetwork(&n, broker_ip, SHELTER_MQTT_BROKER_PORT) != SOCK_OK) {
		    if (disconnect_tick == 0) disconnect_tick = HAL_GetTick();

		    // 강제로 플래그를 끄고 하단의 소켓 폐쇄(Cleanup) 코드로 이동하게 만듦
		    is_mqtt_connected = false;
		    close(MQTT_SOCKET_NUM);
		    osDelay(MQTT_RECONNECT_DELAY);
		    continue;
		}

		/* [Step 5] MQTT 핸드셰이크 및 LWT 설정 */
		MQTTClientInit(&c, &n, 3000, buf, 512, readbuf, 512);
		MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
		data.clientID.cstring         = uuid;
		data.keepAliveInterval        = MQTT_KEEP_ALIVE;
		data.cleansession             = 1;
		data.willFlag                 = 1;
		data.will.topicName.cstring   = topics.tele_state;
		data.will.message.cstring     = "{\"status\":\"OFFLINE\"}";
		data.will.retained            = 1;
		data.will.qos                 = QOS0;

		if (MQTTConnect(&c, &data) == SUCCESS) {
			is_mqtt_connected = true;
			MQTT_Reset_Report_Flags();

			if (disconnect_tick != 0) {
				printf("[MQTT] Restored! (Down: %lu sec)\r\n",
						(HAL_GetTick() - disconnect_tick) / 1000);
				disconnect_tick = 0;
			} else {
				printf("[MQTT] Connected Successfully.\r\n");
			}

			/* ONLINE (프로토콜: uid = STM32 96-bit UID, 장치 식별용) */
			snprintf(payload, sizeof(payload),
					"{\"status\":\"ONLINE\",\"uid\":\"%s\",\"conn\":1}",
					dev_status.uid);
			MQTTMessage pub_msg = {
					.qos = QOS0, .retained = 1, .dup = 0,
					.payload = payload, .payloadlen = (int)strlen(payload)
			};
			MQTTPublish(&c, topics.tele_state, &pub_msg);
			MQTTSubscribe(&c, topics.cmnd_all, QOS0, MQTT_MessageArrived);

			/* [Step 6] 세션 유지 루프 */
			int phy_fail = 0, sr_fail = 0;
			while (is_mqtt_connected) {

				/* 물리 링크 체크 (연속 감지 가드 추가) */
				if ((getPHYCFGR() & 0x01) == 0) {
				    phy_fail++;
				    // 순간적인 1~2회 글리치는 로그를 찍지 않고 무시, 3회 연속(약 180ms) 끊겼을 때만 판단
				    if (phy_fail == 3) {
				        printf("[DROP] Physical Link Down — waiting recovery...\r\n");
				    }
				    if (phy_fail > 7) { // 확정 조건도 조금 더 여유있게 변경 (3 + 5회)
				        printf("[DROP] Physical Link Down (confirmed). Disconnecting.\r\n");
				        break;
				    }
				} else {
				    if (phy_fail >= 3) { // 실제 로그가 찍혔던 상황에서만 복구 로그 출력
				        printf("[NET] Physical Link Recovered (phy_fail was %d).\r\n", phy_fail);
				    }
				    phy_fail = 0;
				}

				/* TCP 소켓 상태 체크 */
				if (getSn_SR(MQTT_SOCKET_NUM) != 0x17) {
					if (++sr_fail > 20) {
						printf("[DROP] TCP Socket Lost (SR=0x%02X). Disconnecting.\r\n",
								getSn_SR(MQTT_SOCKET_NUM));
						break;
					}
				} else {
					sr_fail = 0;
				}

				/* MQTT 패킷 처리 */
				if (MQTTYield(&c, 10) != SUCCESS) {
					printf("[DROP] MQTTYield Failed. Disconnecting.\r\n");
					break;
				}

				osDelay(50); // 루프 주기 ≈ 60ms
			}
		} else {
			printf("[MQTT] Connect Refused (check broker / credentials).\r\n");
		}

		/* =================================================================
                   [Cleanup] ★ 무한 멈춤 결함 완벽 해결 (서버 리부트 자가 치유 엔진)
                   ================================================================= */
		is_mqtt_connected = false;
		if (disconnect_tick == 0) disconnect_tick = HAL_GetTick();

		uint8_t current_sr = getSn_SR(MQTT_SOCKET_NUM);

		// 💡 소켓이 정상 연결(0x17) 상태가 아니라면 disconnect()를 건너뛰고 강제 폐쇄로 직결
		if (current_sr == 0x17) {
			disconnect(MQTT_SOCKET_NUM);
		} else {
			// 🔒 0x24 등 비정상 좀비 상태일 땐 하드웨어 내부 레지스터를 다이렉트로 날려 락을 해제
			setSn_CR(MQTT_SOCKET_NUM, Sn_CR_CLOSE);
			while (getSn_CR(MQTT_SOCKET_NUM)); // 명령 완료 대기
		}

		// 최종 완전 소멸 및 초기화
		close(MQTT_SOCKET_NUM);

		printf("\r\n[SYS_RECOVERY] Zombie Socket Purged. Connection Pipeline Reset!\r\n");
		printf("[MQTT] Will retry in %d ms...\r\n\r\n", MQTT_RECONNECT_DELAY);

		osDelay(MQTT_RECONNECT_DELAY);
	}
}

/* ======================================================================
 [StartAppTimeTask] — 시간 관리 및 밀리초 타이머 갱신
 ====================================================================== */
void StartAppTimeTask(void *argument)
{
	uint32_t last_sec_tick = HAL_GetTick();
	is_time_synced = true;

	for (;;) {
		MilliTimer = HAL_GetTick();
		uint32_t now = HAL_GetTick();

		// 1. 이미 여기서 정확하게 1초에 한 번만 진입하도록 차단막이 걸려 있습니다.
		if (now - last_sec_tick >= 1000) {
			last_sec_tick = now;

			// 2. 중복 조건문 싹 지우고 가차 없이 바로 틱 실행!
			#if SHELTER_NET_USE_DHCP
			W5500_DhcpTick();
			#endif
		}

		osDelay(100);
	}
}

/* ======================================================================
 [StartAppSensorTask] — 환경 센서 및 쿨러 팬 관리
   (Dust, TH_In, TH_Out, Relay — 주기 보고)

 흐름:
   1. 하드웨어 초기화
   2. MQTT 연결 대기
   3. 최초 탐색 (최대 10초)
   4. 부팅 직후 1회 강제 전송
   5. 메인 루프 — PERIODIC_REPORT_MS 주기 보고

 ★ 타이머 드리프트 수정:
   기존 코드는 last_th_out_pub_tick 비교 후 타이머를 now로 갱신했으나,
   MQTTYield(50) + osDelay(200) ≈ 250ms 누적 오차가 매 주기마다 쌓여
   실제 보고 간격이 조금씩 짧아졌습니다.
   → 보고 직후 last_*_pub_tick 을 now가 아닌 HAL_GetTick()으로 갱신하고,
     보고 조건을 하나의 기준 타이머(last_th_out_pub_tick)로 통일합니다.
 ====================================================================== */
void StartAppSensorTask(void *argument)
{
	HCSD_HandleTypeDef h_sensor = hcsd_Init(&hi2c2);
	int retry_in = 0, retry_out = 0;
	char payload[256];
	uint32_t now;

	/* [1. 초기화] */
	DUST_Init(&huart8);
	CWTTH03S_Modbus_Init();
	pub_done_dust = pub_done_th_in = pub_done_th_out = pub_done_relay = false;

	/* [2. MQTT 연결 대기] */
	while (!is_mqtt_connected) osDelay(500);

	/* [3. 초기 탐색 (최대 10초)] */
	for (int i = 0; i < 10; i++) {
		if (hcsd_UpdateValue(&h_sensor) == 0) {
			hcsd_GetTemperatureAndHumidity(&h_sensor,
					&dev_status.th_in.temp, &dev_status.th_in.humi);
			if (dev_status.th_in.temp > 0.1f || dev_status.th_in.temp < -0.1f)
				dev_status.th_in.is_connected = true;
		}
		if (osMutexAcquire(mqtt_mutex_id, 100) == osOK) {
			if (CWTTH03S_Modbus_ReadSensor(&dev_status.th_out) == HAL_OK) {
				if (dev_status.th_out.temp > 0.1f || dev_status.th_out.temp < -0.1f)
					dev_status.th_out.is_connected = true;
			}
			osMutexRelease(mqtt_mutex_id);
		}
		while (DUST_GetReadyData(&dev_status.dust))
			dev_status.dust.is_connected = true;

		if (dev_status.th_in.is_connected && dev_status.th_out.is_connected) break;
		osDelay(1000);
	}

	/* [4. 부팅 직후 1회 강제 전송] */
	if (osMutexAcquire(mqtt_mutex_id, 500) == osOK) {
		snprintf(payload, sizeof(payload),
				"{\"pm1_0\":%d,\"pm2_5\":%d,\"pm10\":%d,\"conn\":%d}",
				dev_status.dust.pm1_0, dev_status.dust.pm2_5,
				dev_status.dust.pm10,  dev_status.dust.is_connected);
		MQTTPublish(&c, topics.tele_dust,
				&(MQTTMessage){QOS0,0,0,0,payload,(int)strlen(payload)});

		snprintf(payload, sizeof(payload),
				"{\"temp\":%.1f,\"humi\":%.1f,\"conn\":%d}",
				dev_status.th_in.temp, dev_status.th_in.humi, dev_status.th_in.is_connected);
		MQTTPublish(&c, topics.tele_th_in,
				&(MQTTMessage){QOS0,0,0,0,payload,(int)strlen(payload)});

		snprintf(payload, sizeof(payload),
				"{\"temp\":%.1f,\"humi\":%.1f,\"conn\":%d}",
				dev_status.th_out.temp, dev_status.th_out.humi, dev_status.th_out.is_connected);
		MQTTPublish(&c, topics.tele_th_out,
				&(MQTTMessage){QOS0,0,0,0,payload,(int)strlen(payload)});

		char r_list[128] = {0};  int r_off = 0;
		for (int i = 0; i < 15; i++)
			r_off += snprintf(r_list+r_off, sizeof(r_list)-r_off,
					"%d%s", dev_status.relay.ch[i]?1:0, (i<14)?",":"");
		snprintf(payload, sizeof(payload), "{\"relays\":[%s],\"conn\":1}", r_list);
		MQTTPublish(&c, topics.tele_ad,
				&(MQTTMessage){QOS0,0,0,0,payload,(int)strlen(payload)});

		/* 보고 시점 기준점을 전송 완료 직후 HAL_GetTick()으로 고정 */
		uint32_t sent_tick = HAL_GetTick();
		last_dust_pub_tick = last_th_in_pub_tick =
				last_th_out_pub_tick = last_state_pub_tick = sent_tick;
		pub_done_dust = pub_done_th_in = pub_done_th_out = pub_done_relay = true;
		osMutexRelease(mqtt_mutex_id);
	}

	/* [5. 메인 루프] */
	for (;;) {
		if (!is_mqtt_connected) { osDelay(1000); continue; }
		now = HAL_GetTick();

		/* --- A. 실시간 데이터 수집 --- */
		while (DUST_GetReadyData(&dev_status.dust)) {
			dev_status.dust.is_connected = true;
			last_dust_recv_tick = now;
		}

		if (now - last_in_recv_tick >= 3000) {
			last_in_recv_tick = now;
			if (hcsd_UpdateValue(&h_sensor) == 0) {
				hcsd_GetTemperatureAndHumidity(&h_sensor,
						&dev_status.th_in.temp, &dev_status.th_in.humi);
				dev_status.th_in.is_connected =
						(dev_status.th_in.temp > 0.1f || dev_status.th_in.temp < -0.1f);
				retry_in = 0;
			} else if (++retry_in >= 30) {
				dev_status.th_in.is_connected = false;
			}
		}

		if (now - last_out_recv_tick >= 2500) {
			if (osMutexAcquire(mqtt_mutex_id, 100) == osOK) {
				if (CWTTH03S_Modbus_ReadSensor(&dev_status.th_out) == HAL_OK) {
					dev_status.th_out.is_connected =
							(dev_status.th_out.temp > 0.1f || dev_status.th_out.temp < -0.1f);
					retry_out = 0;
				} else if (++retry_out >= 10) {
					dev_status.th_out.is_connected = false;
				}
				last_out_recv_tick = now;
				osMutexRelease(mqtt_mutex_id);
			}
		}

		/* --- B. 주기 보고 (PERIODIC_REPORT_MS)
                  기준 타이머를 last_th_out_pub_tick 하나로 통일.
                  이전처럼 보고 완료 후 now를 쓰면 Yield/osDelay 오차가
                  누적되므로 MQTTPublish 직후 HAL_GetTick()을 사용.     --- */
				  if (now - last_th_out_pub_tick >= PERIODIC_REPORT_MS) {
					  if (osMutexAcquire(mqtt_mutex_id, 500) == osOK) {
						  char cur_time[24];
						  Get_Current_Time_Log_Str(cur_time, sizeof(cur_time));
						  printf("[Report_time] : %s\r\n", cur_time);

						  /* Dust */
						  snprintf(payload, sizeof(payload),
								  "{\"pm1_0\":%d,\"pm2_5\":%d,\"pm10\":%d,\"conn\":%d}",
								  dev_status.dust.pm1_0, dev_status.dust.pm2_5,
								  dev_status.dust.pm10,  dev_status.dust.is_connected);
						  MQTTPublish(&c, topics.tele_dust,
								  &(MQTTMessage){QOS0,0,0,0,payload,(int)strlen(payload)});

						  /* TH_IN */
						  snprintf(payload, sizeof(payload),
								  "{\"temp\":%.1f,\"humi\":%.1f,\"conn\":%d}",
								  dev_status.th_in.temp, dev_status.th_in.humi, dev_status.th_in.is_connected);
						  MQTTPublish(&c, topics.tele_th_in,
								  &(MQTTMessage){QOS0,0,0,0,payload,(int)strlen(payload)});

						  /* TH_OUT */
						  snprintf(payload, sizeof(payload),
								  "{\"temp\":%.1f,\"humi\":%.1f,\"conn\":%d}",
								  dev_status.th_out.temp, dev_status.th_out.humi, dev_status.th_out.is_connected);
						  MQTTPublish(&c, topics.tele_th_out,
								  &(MQTTMessage){QOS0,0,0,0,payload,(int)strlen(payload)});

						  /* Relay */
						  char r_list[128] = {0};  int r_off = 0;
						  for (int i = 0; i < 15; i++)
							  r_off += snprintf(r_list+r_off, sizeof(r_list)-r_off,
									  "%d%s", dev_status.relay.ch[i]?1:0, (i<14)?",":"");
						  snprintf(payload, sizeof(payload), "{\"relays\":[%s],\"conn\":1}", r_list);
						  MQTTPublish(&c, topics.tele_ad,
								  &(MQTTMessage){QOS0,0,0,0,payload,(int)strlen(payload)});

						  /* 타이머를 전송 완료 시점으로 갱신 (드리프트 방지) */
						  uint32_t sent_tick = HAL_GetTick();
						  last_th_out_pub_tick = last_th_in_pub_tick =
								  last_dust_pub_tick   = last_state_pub_tick = sent_tick;

						  osMutexRelease(mqtt_mutex_id);
						  printf("[SENSOR] Periodic Report Sent.\r\n");
					  }
				  }

				  MQTTYield(&c, 50);
				  osDelay(200);
	}
}

/* ======================================================================
 [PowerBoardTask] — 전원 보드 관리 (8채널 Modbus 모니터링)
 ====================================================================== */
void PowerBoardTask(void *argument)
{
	char payload[1024] = {0};
	bool     last_sent_pwr_conn  = false;
	int      pwr_fail_cnt        = 0;
	uint32_t reconnect_sync_tick = 0;

	PowerBoard_Init();
	osDelay(1000);

	for (;;) {
		uint32_t now = HAL_GetTick();

		if (!is_mqtt_connected) {
			dev_status.is_pwr_connected = false;
			reconnect_sync_tick = 0;
			osDelay(1000);
			continue;
		}

		/* Step 1: 재연결/부팅 직후 탐색 타이머 시작 */
		if (pub_done_pwr_all == false && reconnect_sync_tick == 0) {
			reconnect_sync_tick = now;
			pwr_fail_cnt = 0;
		}

		/* Step A: 데이터 수집 */
		if (osMutexAcquire(mqtt_mutex_id, 300) == osOK) {
			if (PowerBoard_UpdateAllData(dev_status.pwr_ch) == HAL_OK) {
				dev_status.is_pwr_connected = true;
				pwr_fail_cnt = 0;
			} else {
				if (++pwr_fail_cnt >= 10)
					dev_status.is_pwr_connected = false;
			}
			osMutexRelease(mqtt_mutex_id);
		}

		/* Step B: 보고 판별 */
		bool should_pub_pwr = false;
		if (!pub_done_pwr_all) {
			if (dev_status.is_pwr_connected)
				should_pub_pwr = true;
			else if (now - reconnect_sync_tick > 30000)
				should_pub_pwr = true;
			else {
				osDelay(100);
				continue;
			}
		} else if (now - last_pwr_all_pub_tick >= POWER_REPORT_MS) {
			should_pub_pwr = true;
		} else if (dev_status.is_pwr_connected != last_sent_pwr_conn) {
			should_pub_pwr = true;
		}

		/* Step C: 실제 전송 */
		if (should_pub_pwr) {
			if (osMutexAcquire(mqtt_mutex_id, 500) == osOK) {
				if (!pub_done_pwr_all || (HAL_GetTick() - last_pwr_all_pub_tick > POWER_REPORT_MS)) {
					int len = 0;
					if (dev_status.is_pwr_connected) {
						uint8_t active_id = dev_status.pwr_ch[0].b_id;
						len = snprintf(payload, sizeof(payload),
								"{\"b_id\":%d,\"pb\":[", active_id);
						for (int i = 0; i < MAX_POWER_CHANNELS; i++) {
							int sw = (strcmp(dev_status.pwr_ch[i].pwr, "ON") == 0) ? 1 : 0;
							len += snprintf(payload+len, sizeof(payload)-len,
									"{\"ch\":%d,\"sw\":%d,\"c\":%.2f,\"w\":%.1f,\"v\":%d}%s",
									dev_status.pwr_ch[i].ch, sw,
									dev_status.pwr_ch[i].curr,
									dev_status.pwr_ch[i].watt,
									dev_status.pwr_ch[i].volt,
									(i < MAX_POWER_CHANNELS-1) ? "," : "");
						}
						snprintf(payload+len, sizeof(payload)-len,
								"],\"conn\":1,\"result\":1}");
					} else {
						snprintf(payload, sizeof(payload),
								"{\"b_id\":%d,\"pb\":0,\"conn\":1,\"result\":0}",
								dev_status.pwr_ch[0].b_id);
					}

					if (MQTTPublish(&c, topics.tele_pb,
							&(MQTTMessage){QOS0,0,0,0,payload,(int)strlen(payload)}) == SUCCESS) {
						last_pwr_all_pub_tick    = HAL_GetTick();
						last_sent_pwr_conn       = dev_status.is_pwr_connected;
						pub_done_pwr_all         = true;
					}
				}
				osMutexRelease(mqtt_mutex_id);
			}
		}

		osDelay(800);
	}
}

/* ======================================================================
 [APTask] — Himpel 공기청정기 관리 (RS485 통신)
 ====================================================================== */
void APTask(void *argument)
{
	char    payload[512];
	bool    last_sent_ap_conn  = false;
	int     search_count       = 0;
	uint32_t search_start_tick = 0;

	Himpel_Init();
	osDelay(1000);

	for (;;) {
		uint32_t now = HAL_GetTick();

		if (!is_mqtt_connected) {
			dev_status.ap.is_connected = false;
			search_start_tick = 0;
			search_count      = 0;
			pub_done_ap       = false;
			osDelay(1000);
			continue;
		}

		if (!pub_done_ap && search_start_tick == 0) {
			search_start_tick = now;
			search_count      = 0;
		}

		/* Step A: RS485 질의
           getFanStatus() → HimpelSendData() → HimpelUpdateStatus()
           성공 시 HimpelUpdateStatus 내부에서 Mutex를 획득해 dev_status.ap 갱신.
           여기서는 결과(res)로 search_count, last_ap_recv_tick만 별도 관리. */
		int res = getFanStatus();
		if (osMutexAcquire(mqtt_mutex_id, 200) == osOK) {
			if (res == 0) {
				/* HimpelUpdateStatus에서 is_connected = true 처리됨 */
				last_ap_recv_tick = now;
				if (!pub_done_ap) search_count = 5;
			} else {
				if (!pub_done_ap) search_count++;
				/* 운영 중 10초 이상 응답 없으면 오프라인 (HimpelSendData에서도 처리하나 보강) */
				if (pub_done_ap && (now - last_ap_recv_tick > 10000))
					dev_status.ap.is_connected = false;
			}
			osMutexRelease(mqtt_mutex_id);
		}

		/* Step B: 보고 판별 */
		bool should_pub = false;
		bool search_finished = (search_count >= 5) || (now - search_start_tick > 10000);
		if (!pub_done_ap) {
			if (search_finished) should_pub = true;
		} else {
			if (now - last_ap_pub_tick >= PERIODIC_REPORT_MS)
				should_pub = true;
			else if (dev_status.ap.is_connected != last_sent_ap_conn)
				should_pub = true;
		}

		/* Step C: 전송 */
		if (should_pub) {
			if (osMutexAcquire(mqtt_mutex_id, 500) == osOK) {
				if (dev_status.ap.is_connected) {
					snprintf(payload, sizeof(payload),
							"{\"pwr\":%d,\"mode\":\"%s\",\"spd\":%d,\"uv\":%d,"
							"\"co2\":%d,\"pm25\":%d,\"pm10\":%d,\"pm1_0\":%d,"
							"\"temp\":%.1f,\"temp_out\":%.1f,\"humi\":%d,\"tvoc\":%d,"
							"\"filter\":%d,\"conn\":1}",
							(strcmp(dev_status.ap.pwr,"ON")==0?1:0),
							dev_status.ap.mode, dev_status.ap.fan_speed, dev_status.ap.uv_on,
							dev_status.ap.co2, dev_status.ap.dust_pm25, dev_status.ap.dust_pm10,
							dev_status.ap.dust_pm1_0, dev_status.ap.temp_in, dev_status.ap.temp_out,
							dev_status.ap.humi, dev_status.ap.tvoc, dev_status.ap.filter_life);
				} else {
					snprintf(payload, sizeof(payload), "{\"ap\":0,\"conn\":0}");
				}
				if (MQTTPublish(&c, topics.tele_ap,
						&(MQTTMessage){QOS0,0,0,0,payload,(int)strlen(payload)}) == SUCCESS) {
					last_ap_pub_tick   = HAL_GetTick();
					last_sent_ap_conn  = dev_status.ap.is_connected;
					pub_done_ap        = true;
				}
				osMutexRelease(mqtt_mutex_id);
			}
		}

		osDelay(1500);
	}
}

/* ======================================================================
 [ACTask] — LG 에어컨 관리 (Modbus 통신)
 ★ 수정 사항:
   1. last_ac_pub_tick 로컬 선언 제거 → 전역 변수 사용.
      기존 로컬 선언이 전역을 가려서 MQTT_Reset_Report_Flags()의
      타이머 리셋이 ACTask에 반영되지 않았습니다.
   2. getACStatus() 호출 시 외부 Mutex 제거.
      수정된 modbus_lg.c의 getACStatus()가 내부에서 Mutex를
      직접 관리하므로, 여기서 한 번 더 감싸면 Recursive Lock이
      발생하고 코드 의도가 불명확해집니다.
 ====================================================================== */
void ACTask(void *argument)
{
	char     payload[256]         = {0};
	bool     last_sent_ac_conn    = false;
	int      search_retry_count   = 0;
	bool     is_initial_searching = true;

	Modbus_Init();
	osDelay(1000);

	for (;;) {
		uint32_t now = HAL_GetTick();

		if (!is_mqtt_connected) {
			dev_status.ac.is_connected = false;
			is_initial_searching       = true;
			pub_done_ac                = false;
			search_retry_count         = 0;
			osDelay(1000);
			continue;
		}

		/* 초기 탐색 (최대 5회, 2초 간격)
           ★ getACStatus() 내부에서 Mutex를 관리하므로 외부 Mutex 불필요 */
		if (is_initial_searching) {
			if (search_retry_count < 5) {
				getACStatus(); // 내부에서 Mutex 획득/해제
				if (dev_status.ac.is_connected) {
					is_initial_searching = false;
				} else {
					search_retry_count++;
					osDelay(2000);
					continue;
				}
			} else {
				is_initial_searching = false; // 5회 실패 → 오프라인 보고로
			}
		}

		/* 운영 단계 데이터 업데이트
           ★ 외부 Mutex 제거 — getACStatus 내부에서 처리 */
		getACStatus();

		if (dev_status.ac.is_connected)
			last_ac_recv_tick = now;

		/* 보고 판별 */
		bool should_pub = false;
		if (!pub_done_ac)
			should_pub = true;
		else if (now - last_ac_pub_tick >= PERIODIC_REPORT_MS)
			should_pub = true;
		else if (dev_status.ac.is_connected != last_sent_ac_conn)
			should_pub = true;

		/* 전송 */
		if (should_pub) {
			if (osMutexAcquire(mqtt_mutex_id, 500) == osOK) {
				if (dev_status.ac.is_connected) {
					snprintf(payload, sizeof(payload),
							"{\"pwr\":%d,\"mode\":\"%s\",\"temp\":%.1f,"
							"\"curr\":%.1f,\"spd\":%d,\"err\":%d,\"conn\":1}",
							(strcmp(dev_status.ac.pwr,"ON")==0?1:0),
							dev_status.ac.mode,
							(double)dev_status.ac.temp_sel,
							(double)dev_status.ac.temp_curr,
							dev_status.ac.fan_speed,
							dev_status.ac.error_code);
				} else {
					snprintf(payload, sizeof(payload), "{\"ac\":0,\"conn\":0}");
				}
				if (MQTTPublish(&c, topics.tele_ac,
						&(MQTTMessage){QOS0,0,0,0,payload,(int)strlen(payload)}) == SUCCESS) {
					last_ac_pub_tick   = HAL_GetTick();
					last_sent_ac_conn  = dev_status.ac.is_connected;
					pub_done_ac        = true;
				}
				osMutexRelease(mqtt_mutex_id);
			}
		}

		osDelay(1000);
	}
}

/* ======================================================================
 [FANTask] — 쿨러 팬 제어 및 RPM 모니터링
 ====================================================================== */

/* TIM1 CH3 Input Capture 콜백 — 팬 RPM 측정 */
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
	if (htim->Instance == TIM1 && htim->Channel == HAL_TIM_ACTIVE_CHANNEL_3) {
		uint32_t current_capture = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_3);
		static uint32_t last_capture = 0;
		uint32_t diff = (current_capture >= last_capture)
                    		  ? (current_capture - last_capture)
                    				  : (65535 - last_capture + current_capture + 1);
		if (diff > 0) {
			float instant_rpm = 30000000.0f / diff;
			if (instant_rpm < 10000)  // 튀는 값 가드
					fan_rpm = (fan_rpm * 0.9f) + (instant_rpm * 0.1f);
		}
		last_capture   = current_capture;
		last_signal_tick = HAL_GetTick();
	}
}

void Cooler_init(void)
{
	/* PE13 (TIM1_CH3) Pull-up 강제 설정 */
	GPIO_InitTypeDef GPIO_InitStruct = {0};
	__HAL_RCC_GPIOE_CLK_ENABLE();
	HAL_GPIO_DeInit(GPIOE, GPIO_PIN_13);
	GPIO_InitStruct.Pin       = GPIO_PIN_13;
	GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
	GPIO_InitStruct.Pull      = GPIO_PULLUP;
	GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_LOW;
	GPIO_InitStruct.Alternate = GPIO_AF1_TIM1;
	HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

	/* 타이머 기능 시작 */
	if (HAL_TIM_IC_Start_IT(&htim1, TIM_CHANNEL_3) != HAL_OK)
		printf("[ERR] Fan Sensing Timer Fail!\r\n");
	if (HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2) != HAL_OK)
		printf("[ERR] Fan PWM Timer Fail!\r\n");

	/* 초기 듀티 보정 (0이면 50%로 강제) */
	if (dev_status.fan.duty_percent == 0)
		dev_status.fan.duty_percent = 50;
	__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, dev_status.fan.duty_percent * 10);
}

void FANTask(void *argument)
{
	char     payload[192];
	bool     last_sent_fan_conn  = false;
	uint32_t reconnect_sync_tick = 0;

	Cooler_init();
	osDelay(1000);

	for (;;) {
		uint32_t now = HAL_GetTick();

		if (!is_mqtt_connected) {
			reconnect_sync_tick = 0;
			pub_done_fan        = false;
			osDelay(1000);
			continue;
		}

		if (pub_done_fan == false && reconnect_sync_tick == 0)
			reconnect_sync_tick = now;

		/* A. 팬 속도 제어 (AUTO 모드) */
				if (strcmp(dev_status.fan.mode, "AUTO") == 0) {
					if (dev_status.th_in.is_connected) {
						float cur_t = dev_status.th_in.temp;
						if      (cur_t <= FAN_TEMP_MIN) dev_status.fan.duty_percent = 0;
						else if (cur_t >= FAN_TEMP_MAX) dev_status.fan.duty_percent = 100;
						else dev_status.fan.duty_percent =
								(int)(((cur_t - FAN_TEMP_MIN) / (float)FAN_TEMP_RANGE) * 100.0f);
					} else {
						dev_status.fan.duty_percent = 100; // 센서 이상 시 최대 가동
					}
				}
				__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, dev_status.fan.duty_percent * 10);

				/* B. 물리 연결 진단 */
				uint32_t signal_diff = now - last_signal_tick;
				if (dev_status.fan.duty_percent >= 10) {
					dev_status.fan.is_connected = (signal_diff <= 7000);
				} else {
					dev_status.fan.is_connected = true;
					last_signal_tick = now;
				}

				/* C. 보고 판정 */
				bool should_pub_fan = false;
				if (!pub_done_fan) {
					if (now - reconnect_sync_tick > 5000)
						should_pub_fan = true;
					else { osDelay(500); continue; }
				} else if (now - last_fan_pub_tick >= PERIODIC_REPORT_MS) {
					should_pub_fan = true;
				} else if (dev_status.fan.is_connected != last_sent_fan_conn) {
					should_pub_fan = true;
				}

				/* D. 전송 */
				if (should_pub_fan) {
					if (osMutexAcquire(mqtt_mutex_id, 200) == osOK) {
						if (!pub_done_fan || (HAL_GetTick() - last_fan_pub_tick > 5000)) {
							if (dev_status.fan.is_connected) {
								snprintf(payload, sizeof(payload),
										"{\"mode\":\"%s\",\"duty\":%d,\"conn\":1}",
										dev_status.fan.mode, dev_status.fan.duty_percent);
							} else {
								snprintf(payload, sizeof(payload), "{\"fan\":0,\"conn\":0}");
							}
							if (MQTTPublish(&c, topics.tele_fan,
									&(MQTTMessage){QOS0,0,0,0,payload,(int)strlen(payload)}) == SUCCESS) {
								last_fan_pub_tick   = now;
								last_sent_fan_conn  = dev_status.fan.is_connected;
								pub_done_fan        = true;
							}
						}
						osMutexRelease(mqtt_mutex_id);
					}
				}

				osDelay(800);
	}
}

/* ======================================================================
 [UART RX 콜백] — AP(USART3) / AC(UART4) / Dust(UART8)
 ====================================================================== */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
	if (huart->Instance == USART3) {
		Himpel_UART_ISR(huart);
		return;
	}
	if (huart->Instance == UART4) {
		Modbus_UART_ISR(huart);
		return;
	}
	DUST_UART_ISR(huart);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
	if (huart->Instance == USART3) {
#if SHELTER_RS485_DEBUG_LOG
		printf("[AP] UART error 0x%08lX — restart RX IT\r\n", (unsigned long)huart->ErrorCode);
#endif
		__HAL_UART_CLEAR_OREFLAG(huart);
		__HAL_UART_CLEAR_NEFLAG(huart);
		__HAL_UART_CLEAR_FEFLAG(huart);
		Himpel_RestartRxIT();
		return;
	}
	if (huart->Instance == UART4) {
#if SHELTER_RS485_DEBUG_LOG
		printf("[AC] UART error 0x%08lX — restart RX IT\r\n", (unsigned long)huart->ErrorCode);
#endif
		__HAL_UART_CLEAR_OREFLAG(huart);
		__HAL_UART_CLEAR_NEFLAG(huart);
		__HAL_UART_CLEAR_FEFLAG(huart);
		Modbus_RestartRxIT();
		return;
	}
}

/* ======================================================================
 [현장 테스트 체크리스트] — MX_App_Init() 실행 전·후 순서
  (코드 수정 없이 현장에서 확인할 항목. USB-UART 115200 + MQTT 클라이언트 준비)

  [사전 준비]
  - 네트워크·브로커: Core/Inc/config.h (STATIC/DHCP, MQTT 브로커 IP)
  - mosquitto_sub -h <브로커IP> -t "dev/tele/#" -v

  [1] 부팅·네트워크
      - 시리얼: [W5500] STATIC/DHCP OK, [BOARD] Device UID, [MQTT] Connected
      - dev/tele/state → {"status":"ONLINE","uid":"<24hex UID>","conn":1}

  [2] 전체 스냅샷
      - mosquitto_pub -t dev/cmnd/dev -m "{\"data\":\"ALL_DATA\"}"
      - stat_dev ACK 후 tele 전 토픽 1회씩 수신 확인

  [3] 전원보드 (PB) — UART5, 기존 동작 확인됨
      - dev/tele/pb → b_id 확인 (보통 1, cmnd에 동일 b_id 사용)
      - {"b_id":1,"get_pb":"state"} → stat/pb

  [4] 환경센서 — 먼지(UART8), 온습도(I2C/RS485)
      - dev/tele/dust, th_in, th_out → conn:1, 값 타당성

  [5] 자동문·릴레이 (AD)
      - dev/cmnd/ad -m "{\"get_ad\":\"state\"}" → stat/ad
      - dev/tele/ad relays 배열 15ch

  [6] 쿨러팬 (Fan)
      - dev/tele/fan → mode, duty, conn
      - {"get_fan":"state"} / {"set_fan":{"mode":"MANUAL","duty":50}}

  [7] 공기청정기 (AP, Himpel) — USART3, 9600, Slave ID 0xA1 (W80이면 0xC1)
      - RS485 A/B·종단 확인
      - dev/tele/ap → conn:1, tvoc/temp_out 포함
      - {"get_ap":"state"} → stat/ap
      - {"set_ap":{"pwr":1,"mode":"AUTO","spd":2,"uv":0,"filter_reset":0,"bypass":0,"timer":0}}
      - 실패 시 시리얼: [AP] FRC Mismatch

  [8] 에어컨 (AC, LG Modbus) — UART4, 9600, Slave ID 0x01
      - RS485 A/B·종단 확인
      - dev/tele/ac → conn:1
      - {"get_ac":"state"} → stat/ac
      - 판정: temp=설정온도, curr=실내온도, spd=1~4, err=0
      - temp/spd 틀리면 레지스터 맵 불일치(40001~03 vs 40021~23) → Modbus 스니퍼
      - 실패 시 시리얼: [AC] Modbus CRC error / exception

  [9] AP+AC 동시 운전
      - tele conn 깜빡임 없는지, cmnd 후 stat 값이 실제 장비와 일치하는지

  [10] 알려진 코드 한계 (현장에서 이상 시 참고)
      - AC getACStatus()가 mqtt_mutex 장시간 점유 → AP tele 지연 가능
      - cmnd result:1 이라도 Modbus/Himpel Write 실패 가능 (stat vs 실제 불일치)
      - ALL_DATA는 캐시값 — tele/ap·stat 직접 확인 권장
      - AP filter=100/0 추정값, timer 바이트 매핑 미검증

  [MX_App_Init] — 시스템 초기화 및 태스크 생성
  ====================================================================== */
void MX_App_Init(void)
{
	DS3231_Init(&hi2c3);
	Boot_Sync_Internal_RTC_From_DS3231();
	W5500_Init();
	if (!W5500_ApplyNetwork()) {
		printf("[ERR] W5500 network apply failed (config.h)\r\n");
	}

	osThreadNew(StartMQTTTask,     NULL, &StartMQTT_attributes);      osDelay(1000);
	osThreadNew(StartAppTimeTask,  NULL, &StartAppTime_attributes);   osDelay(300);
	osThreadNew(StartAppSensorTask,NULL, &StartAppSensor_attributes); osDelay(300);
	osThreadNew(PowerBoardTask,    NULL, &PowerBoard_attributes);     osDelay(300);
	osThreadNew(APTask,            NULL, &AP_attributes);             osDelay(300);
	osThreadNew(ACTask,            NULL, &AC_attributes);             osDelay(300);
	osThreadNew(FANTask,           NULL, &FAN_attributes);            osDelay(300);
}
