#include "app.h"
#include "app_loop.h"
#include "config.h"
#include "net_config.h"
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
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/*
 ========================================================================
 [보고 주기 및 타임아웃 설정]
 ======================================================================== */
#define PERIODIC_REPORT_MS 600000 // 10분 (10 * 60 * 1000)
#define POWER_REPORT_MS    10000  // 전원 보드 보고 주기 (10초)
#define DUST_TIMEOUT       5000   // 5초 무응답 → 연결 끊김 판정
#define FAN_TEMP_MIN       0.0f   // 팬 정지 온도 (0%)
#define FAN_TEMP_MAX       20.0f  // 팬 최대 가동 온도 (100%)
#define FAN_TEMP_RANGE     (FAN_TEMP_MAX - FAN_TEMP_MIN)

extern void getAPStatusAll(APData *dest);

/*
 ========================================================================
 [1. 통합 장치 상태 관리 (Global Status Snapshot)]
 ======================================================================== */
DeviceStatus dev_status = { .uid = "", .dust = { .pm1_0 = 0, .pm2_5 = 0, .pm10 =
		0, .is_connected = false }, .th_in = { .temp = 0.0f, .humi = 0.0f,
		.is_connected = false }, .th_out = { .temp = 0.0f, .humi = 0.0f,
		.is_connected = false }, .fan = { .rpm = 0, .duty_percent = 50, .mode =
		"AUTO", .is_connected = false },
		.pwr_ch = { { 1, PB_ID, "OFF", 0.0f, 0.0f, 220 }, { 2, PB_ID, "OFF",
				0.0f, 0.0f, 220 }, { 3, PB_ID, "OFF", 0.0f, 0.0f, 220 }, { 4,
				PB_ID, "OFF", 0.0f, 0.0f, 220 }, { 5, PB_ID, "OFF", 0.0f, 0.0f,
				220 }, { 6, PB_ID, "OFF", 0.0f, 0.0f, 220 }, { 7, PB_ID, "OFF",
				0.0f, 0.0f, 220 }, { 8, PB_ID, "OFF", 0.0f, 0.0f, 220 }, },
		.is_pwr_connected = false, .relay = { .ch = { false, } },
		/* [★v2.0 완전 격리] 형이 명시한 sw1~sw8 멤버 변수명 및 초기화 구조 100% 동기화 적용 */
		.in_stat = { .p4 = false, .p5 = false, .p6 = false, .p7 = false, .cnt4 =
				0, .cnt5 = 0, .cnt6 = 0, .cnt7 = 0, .sw1 = false, .sw2 = false,
				.sw3 = false, .sw4 = false, .sw5 = false, .sw6 = false, .sw7 =
						false, .sw8 = false, .sw_val = 0 }, .ap = {
				.pwr = "OFF", .mode = "AUTO", .fan_speed = 0,
				.filter_life = 100, .uv_on = false, .is_connected = false },
		.ac = { .pwr = "OFF", .mode = "COOL", .temp_sel = 24, .temp_curr = 0,
				.fan_speed = 1, .is_connected = false } };
/*
 ========================================================================
 [2. 시스템 및 MQTT 통신 객체]
 ======================================================================== */
_RTC current_rtc;
MQTTClient c;
MQTT_Topics topics;
osMutexId_t mqtt_mutex_id;
volatile bool is_mqtt_connected = false;
volatile bool is_time_synced = false;
extern unsigned long MilliTimer;

/*
 ========================================================================
 [3. 보고 상태 플래그 및 타이머]
 ======================================================================== */
volatile bool pub_done_pwr[MAX_POWER_CHANNELS] = { false };
volatile bool pub_done_pwr_all = false;
uint32_t last_pwr_all_pub_tick = 0;
uint32_t last_pwr_recv_tick = 0;
volatile bool pub_done_ap = false;
volatile bool pub_done_ac = false;
volatile bool pub_done_fan = false;
volatile bool pub_done_relay = false;
volatile bool pub_done_dust = false;
volatile bool pub_done_th_in = false;
volatile bool pub_done_th_out = false;
volatile bool pub_done_input = false; // 외부 입력 4채널 (v2.0)

uint32_t last_state_pub_tick = 0;
uint32_t last_ap_pub_tick = 0;
uint32_t last_ac_pub_tick = 0;
uint32_t last_fan_pub_tick = 0;
uint32_t last_dust_pub_tick = 0;
uint32_t last_th_in_pub_tick = 0;
uint32_t last_th_out_pub_tick = 0;
uint32_t last_input_pub_tick = 0; // 외부 입력 4채널 (v2.0)

/* 8채널 실제 스위치 상태 변경 감지 독립 비교용 라벨 */
static bool last_sw1 = false, last_sw2 = false, last_sw3 = false, last_sw4 =
		false;
static bool last_sw5 = false, last_sw6 = false, last_sw7 = false, last_sw8 =
		false;

/*
 ========================================================================
 [4. 데이터 백업 변수 (mqtt_handler 동기화용)]
 ======================================================================== */
PowerData last_sent_pwr[MAX_POWER_CHANNELS];
APData last_sent_ap;
ACData last_sent_ac;
DustData last_sent_dust;
THData last_sent_th_in, last_sent_th_out;
FanData last_sent_fan;
int last_sent_fan_duty = -1;
char last_sent_fan_mode[12] = "";
char last_sent_fan_health[12] = "";
bool last_sent_relay[15] = { false };

/*
 ========================================================================
 [5. 하드웨어 진단 및 네트워크 관련]
 ======================================================================== */
uint32_t last_signal_tick = 0;
uint32_t last_broken_tick = 0;
int broken_report_cnt = 0;
volatile float fan_rpm = 0;
uint8_t boot_diag_delay = 5;
uint32_t last_dust_recv_tick = 0;
uint32_t last_in_recv_tick = 0;
uint32_t last_out_recv_tick = 0;
uint32_t last_ap_recv_tick = 0;
uint32_t last_ac_recv_tick = 0;
uint32_t disconnect_tick = 0;
bool show_net_error_log = true;
/*
 ========================================================================
 [MQTT_Reset_Report_Flags]
 재연결 시 모든 도메인 데이터를 서버와 즉시 동기화하기 위해
 플래그·타이머를 일괄 리셋합니다.
 ======================================================================== */
void MQTT_Reset_Report_Flags(void) {
	uint32_t now = HAL_GetTick();
	/* 1. 전원 보드 */
	pub_done_pwr_all = false;
	last_pwr_all_pub_tick = now - HEARTBEAT_INTERVAL;
	for (int i = 0; i < MAX_POWER_CHANNELS; i++) {
		pub_done_pwr[i] = false;
		last_sent_pwr[i].watt = -1.0f;
	}
	/* 2. 가전(AP, AC) 및 팬 */
	pub_done_ap = false;
	last_sent_ap.fan_speed = -1;
	last_ap_pub_tick = now - HEARTBEAT_INTERVAL;
	pub_done_ac = false;
	last_sent_ac.temp_sel = -1;
	last_ac_pub_tick = now - HEARTBEAT_INTERVAL;
	pub_done_fan = false;
	last_sent_fan_duty = -1;
	last_fan_pub_tick = now - HEARTBEAT_INTERVAL;
	/* 3. 환경 센서 */
	pub_done_dust = false;
	last_sent_dust.pm2_5 = -1;
	last_dust_pub_tick = now - HEARTBEAT_INTERVAL;
	pub_done_th_in = false;
	last_sent_th_in.temp = -99.0f;
	last_th_in_pub_tick = now - HEARTBEAT_INTERVAL;
	pub_done_th_out = false;
	last_sent_th_out.temp = -99.0f;
	last_th_out_pub_tick = now - HEARTBEAT_INTERVAL;
	/* 4. 릴레이 및 시스템 */
	pub_done_relay = false;
	last_state_pub_tick = now - HEARTBEAT_INTERVAL;
	for (int i = 0; i < 15; i++)
		last_sent_relay[i] = !dev_status.relay.ch[i]; // 반대값 → 변화 강제 감지
	printf(
			"[MQTT] All Report Flags & Heartbeat Timers Reset. Full Sync Triggered.\r\n");
}

/*
 ========================================================================
 [StartMQTTTask] — MQTT 연결 관리 및 세션 유지
 ======================================================================== */
void StartMQTTTask(void *argument) {
	Network n;
	uint8_t buf[512], readbuf[512];
	char uuid[SHELTER_DEVICE_UID_LEN], payload[128];
	Board_GetDeviceUuid(uuid, sizeof(uuid));
	strncpy(dev_status.uid, uuid, sizeof(dev_status.uid) - 1);
	dev_status.uid[sizeof(dev_status.uid) - 1] = '\0';
	printf("[BOARD] Device UID: %s\r\n", dev_status.uid);

	/* --- 2. 토픽 경로 초기화 --- */
	/* [System & Sensors] */
	snprintf(topics.tele_state, TOPIC_SIZE, "dev/tele/state");
	snprintf(topics.tele_dust, TOPIC_SIZE, "dev/tele/dust");
	snprintf(topics.tele_th_in, TOPIC_SIZE, "dev/tele/th_in");
	snprintf(topics.tele_th_out, TOPIC_SIZE, "dev/tele/th_out");
	snprintf(topics.cmnd_dev, TOPIC_SIZE, "dev/cmnd/dev");
	snprintf(topics.stat_dev, TOPIC_SIZE, "dev/stat/dev");
	snprintf(topics.cmnd_all, TOPIC_SIZE, "dev/cmnd/#");
	/* [Cooler Fan] */
	snprintf(topics.tele_fan, TOPIC_SIZE, "dev/tele/fan");
	snprintf(topics.cmnd_fan_set, TOPIC_SIZE, "dev/cmnd/fan/duty");
	snprintf(topics.cmnd_fan_get, TOPIC_SIZE, "dev/cmnd/fan/get");
	snprintf(topics.stat_fan, TOPIC_SIZE, "dev/stat/fan");
	/* [Relay] */
	snprintf(topics.tele_ad, TOPIC_SIZE, "dev/tele/ad");
	snprintf(topics.cmnd_ad_get, TOPIC_SIZE, "dev/cmnd/ad/get");
	snprintf(topics.stat_ad, TOPIC_SIZE, "dev/stat/ad");
	/* [Himpel AP] */
	snprintf(topics.tele_ap, TOPIC_SIZE, "dev/tele/ap");
	snprintf(topics.cmnd_ap_pwr, TOPIC_SIZE, "dev/cmnd/ap/power");
	snprintf(topics.cmnd_ap_mod, TOPIC_SIZE, "dev/cmnd/ap/mode");
	snprintf(topics.cmnd_ap_spd, TOPIC_SIZE, "dev/cmnd/ap/speed");
	snprintf(topics.cmnd_ap_uv, TOPIC_SIZE, "dev/cmnd/ap/uv");
	snprintf(topics.cmnd_ap_setall, TOPIC_SIZE, "dev/cmnd/ap/set_all");
	snprintf(topics.cmnd_ap_getall, TOPIC_SIZE, "dev/cmnd/ap/get");
	snprintf(topics.stat_ap, TOPIC_SIZE, "dev/stat/ap");
	/* [Power Board] */
	snprintf(topics.tele_pb, TOPIC_SIZE, "dev/tele/pb");
	snprintf(topics.cmnd_pb_ch, TOPIC_SIZE, "dev/cmnd/pb/ch");
	snprintf(topics.cmnd_pb_all, TOPIC_SIZE, "dev/cmnd/pb/all");
	snprintf(topics.cmnd_pb_get, TOPIC_SIZE, "dev/cmnd/pb/get");
	snprintf(topics.stat_pb, TOPIC_SIZE, "dev/stat/pb");
	/* [LG AC] */
	snprintf(topics.tele_ac, TOPIC_SIZE, "dev/tele/ac");
	snprintf(topics.cmnd_ac_pwr, TOPIC_SIZE, "dev/cmnd/ac/power");
	snprintf(topics.cmnd_ac_mod, TOPIC_SIZE, "dev/cmnd/ac/mode");
	snprintf(topics.cmnd_ac_temp, TOPIC_SIZE, "dev/cmnd/ac/temp");
	snprintf(topics.cmnd_ac_spd, TOPIC_SIZE, "dev/cmnd/ac/speed");
	snprintf(topics.cmnd_ac_setall, TOPIC_SIZE, "dev/cmnd/ac/set_all");
	snprintf(topics.cmnd_ac_getall, TOPIC_SIZE, "dev/cmnd/ac/get");
	snprintf(topics.stat_ac, TOPIC_SIZE, "dev/stat/ac");
	/* [외부 입력 4채널] v2.0 신규 */
	snprintf(topics.tele_input, TOPIC_SIZE, "dev/tele/input");
	snprintf(topics.cmnd_input, TOPIC_SIZE, "dev/cmnd/input");
	snprintf(topics.stat_input, TOPIC_SIZE, "dev/stat/input");

	/* --- 3. Mutex 생성 --- */
	const osMutexAttr_t mqtt_mutex_attr = { .name = "mqtt_mutex", .attr_bits =
			osMutexRecursive };
	mqtt_mutex_id = osMutexNew(&mqtt_mutex_attr);
	if (mqtt_mutex_id == NULL)
		printf("[ERR] Mutex Creation Failed!\r\n");

	/* 시간 동기화 대기 */
	while (!is_time_synced)
		osDelay(500);
	/* W5500 재전송 파라미터 */
	setRTR(2000); // 재전송 타이머 200ms
	setRCR(3);    // 재전송 횟수 3회

	/* NTP는 최초 1회 + 이후 24시간 간격으로만 동기화 */
	static uint32_t last_ntp_sync_tick = 0;
#define NTP_SYNC_INTERVAL_MS (24UL * 60UL * 60UL * 1000UL) // 24시간

	uint32_t boot_tick = HAL_GetTick();
	for (;;) {
		is_mqtt_connected = false;
		uint8_t ip[4];
		if (g_net_cfg_pending
				&& (HAL_GetTick() - boot_tick > SHELTER_NET_ROLLBACK_TIMEOUT_MS)) {
			printf(
					"[NETCFG] New config still unconfirmed after %lus. Rebooting for rollback check...\r\n",
					(unsigned long) (SHELTER_NET_ROLLBACK_TIMEOUT_MS / 1000));
			osDelay(300);
			HAL_NVIC_SystemReset();
		}
		/* [Step 1] 물리 소켓 완전 초기화 (Zombie Connection 방지) */
		if (getSn_SR(MQTT_SOCKET_NUM) != SOCK_CLOSED) {
			disconnect(MQTT_SOCKET_NUM);
			setSn_CR(MQTT_SOCKET_NUM, Sn_CR_CLOSE);
			while (getSn_CR(MQTT_SOCKET_NUM))
				;
			uint32_t wait_tick = HAL_GetTick();
			while (getSn_SR(MQTT_SOCKET_NUM) != SOCK_CLOSED
					&& (HAL_GetTick() - wait_tick < 200)) {
				osDelay(1);
			}
		}
		osDelay(200);
		/* [Step 2] 링크 + 유효 IPv4 대기 */
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
						printf(
								"[WAIT] Waiting for network (see config.h STATIC/DHCP)...\r\n");
					}
				} else {
					if (HAL_GetTick() - wait_log_tick >= 10000) {
						wait_log_tick = HAL_GetTick();
						printf("[WAIT] Network not ready (%lu s)\r\n",
								(unsigned long) ((HAL_GetTick()
										- disconnect_tick) / 1000));
					}
					osDelay(1000);
				}
			}
			if (link_was_lost) {
				printf("[NET] Link up. LAN %d.%d.%d.%d\r\n", ip[0], ip[1],
						ip[2], ip[3]);
			}
		}
		/* [Step 3] NTP 시간 동기화 */
		bool do_ntp = (last_ntp_sync_tick == 0)
				|| (HAL_GetTick() - last_ntp_sync_tick >= NTP_SYNC_INTERVAL_MS);
		if (do_ntp) {
			printf("[NTP] Syncing time...\r\n");
			if (W5500_Sync_RTC_From_NTP() == 1) {
				last_ntp_sync_tick = HAL_GetTick();
				Log_With_Time("SYS", "NTP Sync Done.");
			} else {
				Log_With_Time("WARN", "NTP Sync Failed. Using backup time.");
			}
		}
		/* [Step 4] TCP 레이어 연결 — 하드코딩 매크로 전면 영구 박멸 및 내장 플래시 런타임 변수 추적 */
		uint8_t broker_ip[4];
		memcpy(broker_ip, g_net_cfg.broker_ip, 4);
		uint16_t broker_port = g_net_cfg.broker_port;
		printf("[MQTT] Connecting to %u.%u.%u.%u:%d...\r\n", broker_ip[0],
				broker_ip[1], broker_ip[2], broker_ip[3], broker_port);

		NewNetwork(&n, MQTT_SOCKET_NUM);
		if (ConnectNetwork(&n, broker_ip, broker_port) != SOCK_OK) {
			if (disconnect_tick == 0)
				disconnect_tick = HAL_GetTick();
			is_mqtt_connected = false;
			close(MQTT_SOCKET_NUM);
			osDelay(MQTT_RECONNECT_DELAY);
			continue;
		}
		/* [Step 5] MQTT 핸드셰이크 및 LWT 설정 */
		MQTTClientInit(&c, &n, 3000, buf, 512, readbuf, 512);
		MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
		data.clientID.cstring = uuid;
		data.keepAliveInterval = MQTT_KEEP_ALIVE;
		data.cleansession = 1;
		data.willFlag = 1;
		data.will.topicName.cstring = topics.tele_state;
		data.will.message.cstring = "{\"status\":\"OFFLINE\"}";
		data.will.retained = 1;
		data.will.qos = QOS0;
		if (MQTTConnect(&c, &data) == SUCCESS) {
			is_mqtt_connected = true;
			MQTT_Reset_Report_Flags();
			NetConfig_ConfirmBoot();
			if (disconnect_tick != 0) {
				printf("[MQTT] Restored! (Down: %lu sec)\r\n",
						(HAL_GetTick() - disconnect_tick) / 1000);
				disconnect_tick = 0;
			} else {
				printf("[MQTT] Connected Successfully.\r\n");
			}
			/* ONLINE 보팅 스냅샷 조립 ➔ [★교정] 컴파일 매크로 대신 런타임 net_mode 판독 투영 */
			{
				uint8_t cur_ip[4] = { 0 };
				W5500_NetworkReady(cur_ip);
				snprintf(payload, sizeof(payload),
						"{\"status\":\"ONLINE\",\"uid\":\"%s\",\"ip\":\"%d.%d.%d.%d\",\"mode\":\"%s\",\"conn\":1}",
						dev_status.uid, cur_ip[0], cur_ip[1], cur_ip[2],
						cur_ip[3],
						(g_net_cfg.net_mode == 1) ? "DHCP" : "STATIC");
			}
			MQTTMessage pub_msg = { .qos = QOS0, .retained = 1, .dup = 0,
					.payload = payload, .payloadlen = (int) strlen(payload) };
			MQTTPublish(&c, topics.tele_state, &pub_msg);
			MQTTSubscribe(&c, topics.cmnd_all, QOS0, MQTT_MessageArrived);

			/* [Step 6] 세션 유지 루프 */
			int phy_fail = 0, sr_fail = 0;
			while (is_mqtt_connected) {
				/* 물리 링크 체크 */
				if ((getPHYCFGR() & 0x01) == 0) {
					phy_fail++;
					if (phy_fail == 3) {
						printf(
								"[DROP] Physical Link Down — waiting recovery...\r\n");
					}
					if (phy_fail > 7) {
						printf(
								"[DROP] Physical Link Down (confirmed). Disconnecting.\r\n");
						break;
					}
				} else {
					if (phy_fail >= 3) {
						printf(
								"[NET] Physical Link Recovered (phy_fail was %d).\r\n",
								phy_fail);
					}
					phy_fail = 0;
				}
				/* TCP 소켓 상태 체크 */
				if (getSn_SR(MQTT_SOCKET_NUM) != 0x17) {
					if (++sr_fail > 20) {
						uint8_t ir = getSn_IR(MQTT_SOCKET_NUM);
						printf(
								"[DROP] TCP Socket Lost (SR=0x%02X, Sn_IR=0x%02X [DISCON=%d TIMEOUT=%d]). Disconnecting.\r\n",
								getSn_SR(MQTT_SOCKET_NUM), ir,
								(ir & 0x02) ? 1 : 0, (ir & 0x08) ? 1 : 0);
						break;
					}
				} else {
					sr_fail = 0;
				}
				/* ★ [타 AI 분석 기반 초강력 경쟁상태 동기화 크랙 차단 패치]
				 * 다른 가전 태스크(AC/AP/전원)의 MQTTPublish와 클록 버스 간섭이 나지 않도록 뮤텍스 락온!! */
				if (osMutexAcquire(mqtt_mutex_id, 100) == osOK) {
					int yield_res = MQTTYield(&c, 10);
					osMutexRelease(mqtt_mutex_id);
					if (yield_res != SUCCESS) {
						printf("[DROP] MQTTYield Failed. Disconnecting.\r\n");
						break;
					}
				}
				osDelay(50);
			}
		} else {
			printf("[MQTT] Connect Refused (check broker / credentials).\r\n");
		}
		/* [Cleanup] 좀비 소켓 완전 해제 자가 치유 안전 엔진 */
		is_mqtt_connected = false;
		if (disconnect_tick == 0)
			disconnect_tick = HAL_GetTick();
		uint8_t current_sr = getSn_SR(MQTT_SOCKET_NUM);
		if (current_sr == 0x17) {
			disconnect(MQTT_SOCKET_NUM);
		} else {
			setSn_CR(MQTT_SOCKET_NUM, Sn_CR_CLOSE);
			while (getSn_CR(MQTT_SOCKET_NUM))
				;
		}
		close(MQTT_SOCKET_NUM);
		printf(
				"\r\n[SYS_RECOVERY] Zombie Socket Purged. Connection Pipeline Reset!\r\n");
		printf("[MQTT] Will retry in %d ms...\r\n\r\n", MQTT_RECONNECT_DELAY);
		osDelay(MQTT_RECONNECT_DELAY);
	}
}

/*
 ========================================================================
 [StartAppTimeTask] — 시간 관리 및 밀리초 타이머 갱신
 ======================================================================== */
void StartAppTimeTask(void *argument) {
	uint32_t last_sec_tick = HAL_GetTick();
	is_time_synced = true;
	static uint32_t last_ds3231_sync_tick = 0;
#define DS3231_RESYNC_INTERVAL_MS (30UL * 60UL * 1000UL) // 30분

	for (;;) {
		MilliTimer = HAL_GetTick();
		uint32_t now = HAL_GetTick();

		// 1초 주기로 정확하게 1번씩 진입하도록 차단막 작동
		if (now - last_sec_tick >= 1000) {
			last_sec_tick = now;

			// 런타임 모드 스위칭 연동 완료 (w5500_ctrl.c 바인딩 틱 가동)
			W5500_DhcpTick();

			/* 외부 I2C RTC인 DS3231로부터 내부 RTC를 30분마다 초정밀 재동기화 */
			if (now - last_ds3231_sync_tick >= DS3231_RESYNC_INTERVAL_MS) {
				last_ds3231_sync_tick = now;
				Boot_Sync_Internal_RTC_From_DS3231();
			}
		}
		osDelay(100);
	}
}

/*
 ========================================================================
 [StartAppSensorTask] — 환경 센서 및 쿨러 팬 관리
 (Dust, TH_In, TH_Out, Relay — 주기 보고)
 ======================================================================== */
void StartAppSensorTask(void *argument) {
	HCSD_HandleTypeDef h_sensor = hcsd_Init(&hi2c2);
	int retry_in = 0, retry_out = 0;
	char payload[256];
	uint32_t now;

	/* [1. 초기화] */
	DUST_Init(&huart8);
	CWTTH03S_Modbus_Init();
	pub_done_dust = pub_done_th_in = pub_done_th_out = pub_done_relay = false;
	pub_done_input = false; // v2.0: 외부 입력 4채널 최초 보고 트리거 초기화

	/* [2. MQTT 연결 대기] */
	while (!is_mqtt_connected)
		osDelay(500);

	/* [3. 초기 탐색 (최대 10초)] */
	for (int i = 0; i < 10; i++) {
		if (hcsd_UpdateValue(&h_sensor) == 0) {
			hcsd_GetTemperatureAndHumidity(&h_sensor, &dev_status.th_in.temp,
					&dev_status.th_in.humi);
			if (dev_status.th_in.temp > 0.1f || dev_status.th_in.temp < -0.1f)
				dev_status.th_in.is_connected = true;
		}
		if (osMutexAcquire(mqtt_mutex_id, 100) == osOK) {
			if (CWTTH03S_Modbus_ReadSensor(&dev_status.th_out) == HAL_OK) {
				if (dev_status.th_out.temp > 0.1f
						|| dev_status.th_out.temp < -0.1f)
					dev_status.th_out.is_connected = true;
			}
			osMutexRelease(mqtt_mutex_id);
		}
		while (DUST_GetReadyData(&dev_status.dust))
			dev_status.dust.is_connected = true;

		if (dev_status.th_in.is_connected && dev_status.th_out.is_connected)
			break;
		osDelay(1000);
	}
	/* [4. 부팅 직후 1회 강제 전송] */
	if (osMutexAcquire(mqtt_mutex_id, 500) == osOK) {
		snprintf(payload, sizeof(payload),
				"{\"pm1_0\":%d,\"pm2_5\":%d,\"pm10\":%d,\"conn\":%d}",
				dev_status.dust.pm1_0, dev_status.dust.pm2_5,
				dev_status.dust.pm10, dev_status.dust.is_connected);
		MQTTPublish(&c, topics.tele_dust, &(MQTTMessage ) { QOS0, 0, 0, 0,
						payload, (int) strlen(payload) });

		snprintf(payload, sizeof(payload),
				"{\"temp\":%.1f,\"humi\":%.1f,\"conn\":%d}",
				dev_status.th_in.temp, dev_status.th_in.humi,
				dev_status.th_in.is_connected);
		MQTTPublish(&c, topics.tele_th_in, &(MQTTMessage ) { QOS0, 0, 0, 0,
						payload, (int) strlen(payload) });

		snprintf(payload, sizeof(payload),
				"{\"temp\":%.1f,\"humi\":%.1f,\"conn\":%d}",
				dev_status.th_out.temp, dev_status.th_out.humi,
				dev_status.th_out.is_connected);
		MQTTPublish(&c, topics.tele_th_out, &(MQTTMessage ) { QOS0, 0, 0, 0,
						payload, (int) strlen(payload) });

		char r_list[128] = { 0 };
		int r_off = 0;
		for (int i = 0; i < 15; i++)
			r_off += snprintf(r_list + r_off, sizeof(r_list) - r_off, "%d%s",
					dev_status.relay.ch[i] ? 1 : 0, (i < 14) ? "," : "");
		snprintf(payload, sizeof(payload), "{\"relays\":[%s],\"conn\":1}",
				r_list);
		MQTTPublish(&c, topics.tele_ad, &(MQTTMessage ) { QOS0, 0, 0, 0,
						payload, (int) strlen(payload) });

		/* v2.0: 외부 입력 4채널 최초 상태 발행 */
		SCAN_External_Inputs();
		snprintf(payload, sizeof(payload),
				"{\"in\":[%d,%d,%d,%d],\"count\":[%lu,%lu,%lu,%lu],\"conn\":1}",
				dev_status.in_stat.p4, dev_status.in_stat.p5,
				dev_status.in_stat.p6, dev_status.in_stat.p7,
				(unsigned long) dev_status.in_stat.cnt4,
				(unsigned long) dev_status.in_stat.cnt5,
				(unsigned long) dev_status.in_stat.cnt6,
				(unsigned long) dev_status.in_stat.cnt7);
		MQTTPublish(&c, topics.tele_input, &(MQTTMessage ) { QOS0, 0, 0, 0,
						payload, (int) strlen(payload) });
		pub_done_input = true;

		uint32_t sent_tick = HAL_GetTick();
		last_dust_pub_tick = last_th_in_pub_tick = last_th_out_pub_tick =
				last_state_pub_tick = sent_tick;
		last_input_pub_tick = sent_tick;
		pub_done_dust = pub_done_th_in = pub_done_th_out = pub_done_relay =
				true;
		osMutexRelease(mqtt_mutex_id);
	}

	/* [5. 메인 루프] */
	for (;;) {
		if (!is_mqtt_connected) {
			osDelay(1000);
			continue;
		}
		now = HAL_GetTick();

		/* --- A. 실시간 데이터 수집 --- */
		while (DUST_GetReadyData(&dev_status.dust)) {
			dev_status.dust.is_connected = true;
			last_dust_recv_tick = now;
		}

		/* --- A-1. [v2.0] 기존 4채널 외부 입력 스캔 + 변경 시 즉시 보고 --- */
		SCAN_External_Inputs();
		if (!pub_done_input) {
			if (osMutexAcquire(mqtt_mutex_id, 200) == osOK) {
				snprintf(payload, sizeof(payload),
						"{\"in\":[%d,%d,%d,%d],\"count\":[%lu,%lu,%lu,%lu],\"conn\":1}",
						dev_status.in_stat.p4, dev_status.in_stat.p5,
						dev_status.in_stat.p6, dev_status.in_stat.p7,
						(unsigned long) dev_status.in_stat.cnt4,
						(unsigned long) dev_status.in_stat.cnt5,
						(unsigned long) dev_status.in_stat.cnt6,
						(unsigned long) dev_status.in_stat.cnt7);
				MQTTPublish(&c, topics.tele_input, &(MQTTMessage ) { QOS0, 0, 0,
								0, payload, (int) strlen(payload) });
				pub_done_input = true;
				last_input_pub_tick = HAL_GetTick();
				osMutexRelease(mqtt_mutex_id);
			}
		}

		/* ★ [v2.0 신규 독립형 8채널 실제 스위치 스캔 엔진 가동]
		 * 형이 정해준 전송 무관 내부 갱신 및 실시간 Active Low 판독 로그 출력 축 가동 */
		SCAN_External_Inputs_switch8();

		if (now - last_in_recv_tick >= 3000) {
			last_in_recv_tick = now;
			if (hcsd_UpdateValue(&h_sensor) == 0) {
				hcsd_GetTemperatureAndHumidity(&h_sensor,
						&dev_status.th_in.temp, &dev_status.th_in.humi);
				dev_status.th_in.is_connected = (dev_status.th_in.temp > 0.1f
						|| dev_status.th_in.temp < -0.1f);
				retry_in = 0;
			} else if (++retry_in >= 30) {
				dev_status.th_in.is_connected = false;
			}
		}
		if (now - last_out_recv_tick >= 2500) {
			if (osMutexAcquire(mqtt_mutex_id, 100) == osOK) {
				if (CWTTH03S_Modbus_ReadSensor(&dev_status.th_out) == HAL_OK) {
					dev_status.th_out.is_connected = (dev_status.th_out.temp
							> 0.1f || dev_status.th_out.temp < -0.1f);
					retry_out = 0;
				} else if (++retry_out >= 10) {
					dev_status.th_out.is_connected = false;
				}
				last_out_recv_tick = now;
				osMutexRelease(mqtt_mutex_id);
			}
		}

		/* --- B. 주기 보고 (PERIODIC_REPORT_MS)
		 * 기준 타이머를 하나로 통일하고, osDelay 오차가 누적되는 결함을 막기 위해
		 * MQTTPublish 직후 전송 완료 시점의 HAL_GetTick()으로 동기화 갱신합니다. --- */
		if (now - last_th_out_pub_tick >= PERIODIC_REPORT_MS) {
			if (osMutexAcquire(mqtt_mutex_id, 500) == osOK) {
				char cur_time[24];
				Get_Current_Time_Log_Str(cur_time, sizeof(cur_time));
				printf("[Report_time] : %s\r\n", cur_time);

				/* Dust */
				snprintf(payload, sizeof(payload),
						"{\"pm1_0\":%d,\"pm2_5\":%d,\"pm10\":%d,\"conn\":%d}",
						dev_status.dust.pm1_0, dev_status.dust.pm2_5,
						dev_status.dust.pm10, dev_status.dust.is_connected);
				MQTTPublish(&c, topics.tele_dust, &(MQTTMessage ) { QOS0, 0, 0,
								0, payload, (int) strlen(payload) });

				/* TH_IN */
				snprintf(payload, sizeof(payload),
						"{\"temp\":%.1f,\"humi\":%.1f,\"conn\":%d}",
						dev_status.th_in.temp, dev_status.th_in.humi,
						dev_status.th_in.is_connected);
				MQTTPublish(&c, topics.tele_th_in, &(MQTTMessage ) { QOS0, 0, 0,
								0, payload, (int) strlen(payload) });

				/* TH_OUT */
				snprintf(payload, sizeof(payload),
						"{\"temp\":%.1f,\"humi\":%.1f,\"conn\":%d}",
						dev_status.th_out.temp, dev_status.th_out.humi,
						dev_status.th_out.is_connected);
				MQTTPublish(&c, topics.tele_th_out, &(MQTTMessage ) { QOS0, 0,
								0, 0, payload, (int) strlen(payload) });

				/* Relay */
				char r_list[128] = { 0 };
				int r_off = 0;
				for (int i = 0; i < 15; i++)
					r_off += snprintf(r_list + r_off, sizeof(r_list) - r_off,
							"%d%s", dev_status.relay.ch[i] ? 1 : 0,
							(i < 14) ? "," : "");
				snprintf(payload, sizeof(payload),
						"{\"relays\":[%s],\"conn\":1}", r_list);
				MQTTPublish(&c, topics.tele_ad, &(MQTTMessage ) { QOS0, 0, 0, 0,
								payload, (int) strlen(payload) });

				/* [v2.0] 외부 입력 4채널 — 변경이 없어도 10분마다 주기 재확인 발행 */
				snprintf(payload, sizeof(payload),
						"{\"in\":[%d,%d,%d,%d],\"count\":[%lu,%lu,%lu,%lu],\"conn\":1}",
						dev_status.in_stat.p4, dev_status.in_stat.p5,
						dev_status.in_stat.p6, dev_status.in_stat.p7,
						(unsigned long) dev_status.in_stat.cnt4,
						(unsigned long) dev_status.in_stat.cnt5,
						(unsigned long) dev_status.in_stat.cnt6,
						(unsigned long) dev_status.in_stat.cnt7);
				MQTTPublish(&c, topics.tele_input, &(MQTTMessage ) { QOS0, 0, 0,
								0, payload, (int) strlen(payload) });

				/* 타이머를 전송 완료 시점으로 깔끔하게 마감 (타이머 드리프트 차단) */
				uint32_t sent_tick = HAL_GetTick();
				last_th_out_pub_tick = last_th_in_pub_tick =
						last_dust_pub_tick = last_state_pub_tick = sent_tick;
				last_input_pub_tick = sent_tick;
				osMutexRelease(mqtt_mutex_id);
				printf("[SENSOR] Periodic Report Sent.\r\n");
			}
		}
		MQTTYield(&c, 50);
		osDelay(200);
	}
}

/*
 ========================================================================
 [PowerBoardTask] — 전원 보드 관리 (8채널 Modbus 모니터링)
 ======================================================================== */
void PowerBoardTask(void *argument) {
	char payload[1024] = { 0 };
	bool last_sent_pwr_conn = false;
	int pwr_fail_cnt = 0;
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
				if (!pub_done_pwr_all
						|| (HAL_GetTick() - last_pwr_all_pub_tick
								> POWER_REPORT_MS)) {
					int len = 0;
					if (dev_status.is_pwr_connected) {
						uint8_t active_id = dev_status.pwr_ch[0].b_id;
						len = snprintf(payload, sizeof(payload),
								"{\"b_id\":%d,\"pb\":[", active_id);
						for (int i = 0; i < MAX_POWER_CHANNELS; i++) {
							int sw =
									(strcmp(dev_status.pwr_ch[i].pwr, "ON") == 0) ?
											1 : 0;
							len +=
									snprintf(payload + len,
											sizeof(payload) - len,
											"{\"ch\":%d,\"sw\":%d,\"c\":%.2f,\"w\":%.1f,\"v\":%d}%s",
											dev_status.pwr_ch[i].ch, sw,
											dev_status.pwr_ch[i].curr,
											dev_status.pwr_ch[i].watt,
											dev_status.pwr_ch[i].volt,
											(i < MAX_POWER_CHANNELS - 1) ?
													"," : "");
						}
						snprintf(payload + len, sizeof(payload) - len,
								"],\"conn\":1,\"result\":1}");
					} else {
						snprintf(payload, sizeof(payload),
								"{\"b_id\":%d,\"pb\":0,\"conn\":1,\"result\":0}",
								dev_status.pwr_ch[0].b_id);
					}
					if (MQTTPublish(&c, topics.tele_pb, &(MQTTMessage ) { QOS0,
									0, 0, 0, payload, (int) strlen(payload) })
							== SUCCESS) {
						last_pwr_all_pub_tick = HAL_GetTick();
						last_sent_pwr_conn = dev_status.is_pwr_connected;
						pub_done_pwr_all = true;
					}
				}
				osMutexRelease(mqtt_mutex_id);
			}
		}
		osDelay(800);
	}
}
void APTask(void *argument) {
	char payload[512]; // ⚠️ [교정] 1바이트 변수를 512바이트 대형 배열 버퍼로 원상 복구!
	bool last_sent_ap_conn = false;
	int search_count = 0;
	uint32_t search_start_tick = 0;
	Himpel_Init();
	osDelay(1000);

	for (;;) {
		uint32_t now = HAL_GetTick();
		if (!is_mqtt_connected) {
			dev_status.ap.is_connected = false;
			search_start_tick = 0;
			search_count = 0;
			pub_done_ap = false;
			osDelay(1000);
			continue;
		}
		if (!pub_done_ap && search_start_tick == 0) {
			search_start_tick = now;
			search_count = 0;
		}
		int res = getFanStatus();
		if (osMutexAcquire(mqtt_mutex_id, 200) == osOK) {
			if (res == 0) {
				last_ap_recv_tick = now;
				if (!pub_done_ap)
					search_count = 5;
			} else {
				if (!pub_done_ap)
					search_count++;
				if (pub_done_ap && (now - last_ap_recv_tick > 10000))
					dev_status.ap.is_connected = false;
			}
			osMutexRelease(mqtt_mutex_id);
		}
		bool should_pub = false;
		bool search_finished = (search_count >= 5) || (now - search_start_tick > 10000);
		if (!pub_done_ap) {
			if (search_finished)
				should_pub = true;
		} else {
			if (now - last_ap_pub_tick >= PERIODIC_REPORT_MS)
				should_pub = true;
			else if (dev_status.ap.is_connected != last_sent_ap_conn)
				should_pub = true;
		}
		if (should_pub) {
			if (osMutexAcquire(mqtt_mutex_id, 500) == osOK) {
				if (dev_status.ap.is_connected) {
					snprintf(payload, sizeof(payload),
							"{\"pwr\":%d,\"mode\":\"%s\",\"spd\":%d,\"uv\":%d,\"co2\":%d,\"pm25\":%d,\"pm10\":%d,\"pm1_0\":%d,\"temp\":%.1f,\"temp_out\":%.1f,\"humi\":%d,\"tvoc\":%d,\"filter\":%d,\"conn\":1}",
							(strcmp(dev_status.ap.pwr, "ON") == 0 ? 1 : 0),
							dev_status.ap.mode, dev_status.ap.fan_speed,
							dev_status.ap.uv_on, dev_status.ap.co2,
							dev_status.ap.dust_pm25, dev_status.ap.dust_pm10,
							dev_status.ap.dust_pm1_0, dev_status.ap.temp_in,
							dev_status.ap.temp_out, dev_status.ap.humi,
							dev_status.ap.tvoc, dev_status.ap.filter_life);
				} else {
					snprintf(payload, sizeof(payload), "{\"ap\":0,\"conn\":0}");
				}

				// ⚠️ [정렬] 가독성을 높이고 인자 매칭 오류를 방지하기 위해 구조체 할당 정렬
				MQTTMessage ap_msg = { QOS0, 0, 0, 0, payload, (int)strlen(payload) };
				if (MQTTPublish(&c, topics.tele_ap, &ap_msg) == SUCCESS) {
					last_ap_pub_tick = HAL_GetTick();
					last_sent_ap_conn = dev_status.ap.is_connected;
					pub_done_ap = true;
				}
				osMutexRelease(mqtt_mutex_id);
			}
		}
		osDelay(1500);
	}
}


void ACTask(void *argument) {
	char payload[256] = { 0 }; // ⚠️ [교정] 1바이트 변수를 256바이트 배열 버퍼 공간으로 완전 원상 복구!
	bool last_sent_ac_conn = false;
	int search_retry_count = 0;
	bool is_initial_searching = true;
	Modbus_Init();
	osDelay(1000);

	for (;;) {
		uint32_t now = HAL_GetTick();
		if (!is_mqtt_connected) {
			dev_status.ac.is_connected = false;
			is_initial_searching = true;
			pub_done_ac = false;
			search_retry_count = 0;
			osDelay(1000);
			continue;
		}
		if (is_initial_searching) {
			if (search_retry_count < 5) {
				getACStatus();
				if (dev_status.ac.is_connected) {
					is_initial_searching = false;
				} else {
					search_retry_count++;
					osDelay(2000);
					continue;
				}
			} else {
				is_initial_searching = false;
			}
		}
		getACStatus();
		if (dev_status.ac.is_connected)
			last_ac_recv_tick = now;
		bool should_pub = false;
		if (!pub_done_ac)
			should_pub = true;
		else if (now - last_ac_pub_tick >= PERIODIC_REPORT_MS)
			should_pub = true;
		else if (dev_status.ac.is_connected != last_sent_ac_conn)
			should_pub = true;
		if (should_pub) {
			if (osMutexAcquire(mqtt_mutex_id, 500) == osOK) {
				if (dev_status.ac.is_connected) {
					snprintf(payload, sizeof(payload),
							"{\"pwr\":%d,\"mode\":\"%s\",\"temp\":%.1f,\"curr\":%.1f,\"spd\":%d,\"err\":%d,\"conn\":1}",
							(strcmp(dev_status.ac.pwr, "ON") == 0 ? 1 : 0),
							dev_status.ac.mode, (double) dev_status.ac.temp_sel,
							(double) dev_status.ac.temp_curr,
							dev_status.ac.fan_speed, dev_status.ac.error_code);
				} else {
					snprintf(payload, sizeof(payload), "{\"ac\":0,\"conn\":0}");
				}

				// ⚠️ [정렬] 가독성을 높이고 인자 매칭 오류를 방지하기 위해 구조체 할당 정렬
				MQTTMessage ac_msg = { QOS0, 0, 0, 0, payload, (int)strlen(payload) };
				if (MQTTPublish(&c, topics.tele_ac, &ac_msg) == SUCCESS) {
					last_ac_pub_tick = HAL_GetTick();
					last_sent_ac_conn = dev_status.ac.is_connected;
					pub_done_ac = true;
				}
				osMutexRelease(mqtt_mutex_id);
			}
		}
		osDelay(1000);
	}
}


/*
 ========================================================================
 [FANTask] — 쿨러 팬 제어 및 RPM 모니터링
 ======================================================================== */
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim) {
	if (htim->Instance == TIM1 && htim->Channel == HAL_TIM_ACTIVE_CHANNEL_3) {
		uint32_t current_capture = HAL_TIM_ReadCapturedValue(htim,
				TIM_CHANNEL_3);
		static uint32_t last_capture = 0;
		uint32_t diff =
				(current_capture >= last_capture) ?
						(current_capture - last_capture) :
						(65535 - last_capture + current_capture + 1);
		if (diff > 0) {
			float instant_rpm = 30000000.0f / diff;
			if (instant_rpm < 10000)
				fan_rpm = (fan_rpm * 0.9f) + (instant_rpm * 0.1f);
		}
		last_capture = current_capture;
		last_signal_tick = HAL_GetTick();
	}
}

void Cooler_init(void) {
	GPIO_InitTypeDef GPIO_InitStruct = { 0 };
	__HAL_RCC_GPIOE_CLK_ENABLE();
	HAL_GPIO_DeInit(GPIOE, GPIO_PIN_13);
	GPIO_InitStruct.Pin = GPIO_PIN_13;
	GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
	GPIO_InitStruct.Pull = GPIO_PULLUP;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	GPIO_InitStruct.Alternate = GPIO_AF1_TIM1;
	HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);
	if (HAL_TIM_IC_Start_IT(&htim1, TIM_CHANNEL_3) != HAL_OK)
		printf("[ERR] Fan Sensing Timer Fail!\r\n");
	if (HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2) != HAL_OK)
		printf("[ERR] Fan PWM Timer Fail!\r\n");
	if (dev_status.fan.duty_percent == 0)
		dev_status.fan.duty_percent = 50;
	__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2,
			dev_status.fan.duty_percent * 10);
}

void FANTask(void *argument) {
	char payload[192]; // ⚠️ [교정] 1바이트 변수를 192바이트 배열 버퍼 공간으로 완벽히 복원!
	bool last_sent_fan_conn = false;
	uint32_t reconnect_sync_tick = 0;
	Cooler_init();
	osDelay(1000);

	for (;;) {
		uint32_t now = HAL_GetTick();
		if (!is_mqtt_connected) {
			reconnect_sync_tick = 0;
			pub_done_fan = false;
			osDelay(1000);
			continue;
		}
		if (pub_done_fan == false && reconnect_sync_tick == 0)
			reconnect_sync_tick = now;
		if (strcmp(dev_status.fan.mode, "AUTO") == 0) {
			if (dev_status.th_in.is_connected) {
				float cur_t = dev_status.th_in.temp;
				if (cur_t <= FAN_TEMP_MIN)
					dev_status.fan.duty_percent = 0;
				else if (cur_t >= FAN_TEMP_MAX)
					dev_status.fan.duty_percent = 100;
				else
					dev_status.fan.duty_percent = (int) (((cur_t - FAN_TEMP_MIN)
							/ (float) FAN_TEMP_RANGE) * 100.0f);
			} else {
				dev_status.fan.duty_percent = 100;
			}
		}
		__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2,
				dev_status.fan.duty_percent * 10);
		uint32_t signal_diff = now - last_signal_tick;
		if (dev_status.fan.duty_percent >= 10)
			dev_status.fan.is_connected = (signal_diff <= 7000);
		else {
			dev_status.fan.is_connected = true;
			last_signal_tick = now;
		}
		bool should_pub_fan = false;
		if (!pub_done_fan) {
			if (now - reconnect_sync_tick > 5000)
				should_pub_fan = true;
			else {
				osDelay(500);
				continue;
			}
		} else if (now - last_fan_pub_tick >= PERIODIC_REPORT_MS) {
			should_pub_fan = true;
		} else if (dev_status.fan.is_connected != last_sent_fan_conn) {
			should_pub_fan = true;
		}
		if (should_pub_fan) {
			if (osMutexAcquire(mqtt_mutex_id, 200) == osOK) {
				if (!pub_done_fan
						|| (HAL_GetTick() - last_fan_pub_tick > 5000)) {
					if (dev_status.fan.is_connected)
						snprintf(payload, sizeof(payload),
								"{\"mode\":\"%s\",\"duty\":%d,\"conn\":1}",
								dev_status.fan.mode,
								dev_status.fan.duty_percent);
					else
						snprintf(payload, sizeof(payload),
								"{\"fan\":0,\"conn\":0}");

					// ⚠️ [정렬] 가독성을 높이고 컴파일러 인자 유실을 방지하기 위한 구조체 명시화
					MQTTMessage fan_msg = { QOS0, 0, 0, 0, payload, (int)strlen(payload) };
					if (MQTTPublish(&c, topics.tele_fan, &fan_msg) == SUCCESS) {
						last_fan_pub_tick = HAL_GetTick(); // ⚠️ 옛날 틱인 now 대신 실제 완료 시점 틱으로 교정 (드리프트 방지)
						last_sent_fan_conn = dev_status.fan.is_connected;
						pub_done_fan = true;
					}
				}
				osMutexRelease(mqtt_mutex_id);
			}
		}
		osDelay(800);
	}
}


/*
 ========================================================================
 [UART 인터럽트 이탈 처리 콜백 루틴]
 ======================================================================== */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
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
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) {
	if (huart->Instance == USART3) {
		__HAL_UART_CLEAR_OREFLAG(huart);
		__HAL_UART_CLEAR_NEFLAG(huart);
		__HAL_UART_CLEAR_FEFLAG(huart);
		Himpel_RestartRxIT();
		return;
	}
	if (huart->Instance == UART4) {
		__HAL_UART_CLEAR_OREFLAG(huart);
		__HAL_UART_CLEAR_NEFLAG(huart);
		__HAL_UART_CLEAR_FEFLAG(huart);
		Modbus_RestartRxIT();
		return;
	}
}
/*
 [MX_App_Init] — 하드웨어 극초기 전위 정렬 및 OS 멀티태스크 시동
 ======================================================================== */
void MX_App_Init(void) {
	DS3231_Init(&hi2c3);
	Boot_Sync_Internal_RTC_From_DS3231();
	/* 데이터 캐시 강제 무력화 가드 개방 (H7 부팅 오염 방지) */
	SCB_InvalidateDCache();
	SCB_DisableDCache();
	/* 내장 플래시 무결성 프로파일 메모리 적재 */
	NetConfig_CheckRollback();
	NetConfig_Load();
	/* 캐시 개방 원복 */
	SCB_EnableDCache();
	/* W5500 하드웨어 초기화 및 런타임 동적 스위칭 네트워크 적용 */
	W5500_Init();
	if (!W5500_ApplyNetwork()) {
		printf("[ERR] W5500 network apply failed (config.h)\r\n");
	}
	/* 실시간 스마트 쉘터 제어 OS 멀티 태스크 스케줄링 가동 */
	osThreadNew(StartMQTTTask, NULL, &StartMQTT_attributes);osDelay(1000);
	osThreadNew(StartAppTimeTask, NULL, &StartAppTime_attributes);osDelay(300);
	osThreadNew(StartAppSensorTask, NULL, &StartAppSensor_attributes);osDelay(300);
	osThreadNew(PowerBoardTask, NULL, &PowerBoard_attributes);osDelay(300);
	osThreadNew(APTask, NULL, &AP_attributes);osDelay(300);
	osThreadNew(ACTask, NULL, &AC_attributes);osDelay(300);
	osThreadNew(FANTask, NULL, &FAN_attributes);osDelay(300);
}

