#include "mqtt_handler.h"
#include "app.h"
#include "net_config.h"
#include "power_board.h"
#include "himpel.h"
#include "modbus_lg.h"
#include "app_loop.h"
#include "ds3231m.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

extern void getAPStatusAll(APData* dest);

#include "stm32h7xx_hal.h"

/* ── 지연 stat 발행 큐 (MQTTYield 콜백 내부 MQTTPublish → buf 충돌/deadlock 방지) ── */
#define MQTT_DEFER_MAX 24
#define MQTT_DEFER_PAYLOAD 640

typedef struct {
	char topic[TOPIC_SIZE];
	char payload[MQTT_DEFER_PAYLOAD];
} MqttDeferItem;

static MqttDeferItem g_defer_q[MQTT_DEFER_MAX];
static volatile uint8_t g_defer_head;
static volatile uint8_t g_defer_tail;

typedef enum {
	MQTT_PENDING_NONE = 0,
	MQTT_PENDING_DEV_RESET,
	MQTT_PENDING_SET_NET,
	MQTT_PENDING_SET_BROKER,
	MQTT_PENDING_SET_NET_RESET
} MqttPendingAction;

static volatile MqttPendingAction g_pending_action = MQTT_PENDING_NONE;
static NetRuntimeConfig g_pending_net_cfg;

/* set_ad 15ch 순차 제어 + 기타 장치 하드웨어 지연 실행 (콜백 내 RS485/Modbus/릴레이/osDelay 금지) */
static volatile bool g_ad_seq_pending;
static char g_ad_seq_str[16];

typedef enum {
	MQTT_HW_NONE = 0,
	MQTT_HW_AD_CH,
	MQTT_HW_PB_SINGLE,
	MQTT_HW_PB_ALL,
	MQTT_HW_PB_GET,
	MQTT_HW_FAN_SET,
	MQTT_HW_AC,
	MQTT_HW_AP,
} MqttHwPending;

static volatile MqttHwPending g_hw_pending = MQTT_HW_NONE;
static int g_hw_ad_ch;
static int g_hw_ad_val;
static int g_hw_pb_id;
static int g_hw_pb_ch;
static int g_hw_pb_sw;
static char g_hw_pb_set[9];
static char g_hw_fan_mode[12];
static int g_hw_fan_duty;
static char g_hw_payload[256];
static bool g_hw_is_query;

static void mqtt_build_ad_stat(char *response, size_t resp_sz, bool is_valid);

static void mqtt_publish_deferred(const char *topic, const char *payload)
{
	uint8_t next = (uint8_t)((g_defer_head + 1) % MQTT_DEFER_MAX);
	if (next == g_defer_tail) {
		printf("[MQTT] defer queue full, drop: %s\r\n", topic);
		return;
	}
	MqttDeferItem *slot = &g_defer_q[g_defer_head];
	strncpy(slot->topic, topic, TOPIC_SIZE - 1);
	slot->topic[TOPIC_SIZE - 1] = '\0';
	strncpy(slot->payload, payload, MQTT_DEFER_PAYLOAD - 1);
	slot->payload[MQTT_DEFER_PAYLOAD - 1] = '\0';
	g_defer_head = next;
}

void MqttDeferred_Flush(void)
{
	while (g_defer_tail != g_defer_head) {
		MqttDeferItem *item = &g_defer_q[g_defer_tail];
		int pub_rc = MQTTPublish(&c, item->topic,
				&(MQTTMessage){QOS0, 0, 0, 0, item->payload, (int)strlen(item->payload)});
		if (pub_rc == SUCCESS) {
			printf("[MQTT TX] Topic: %s,%s\r\n", item->topic, item->payload);
			if (strstr(item->topic, "stat/ad") != NULL) {
				pub_done_relay = true;
				last_state_pub_tick = HAL_GetTick();
			} else if (strstr(item->topic, "stat/fan") != NULL) {
				pub_done_fan = true;
				last_fan_pub_tick = HAL_GetTick();
				last_sent_fan_duty = dev_status.fan.duty_percent;
				strncpy(last_sent_fan_mode, dev_status.fan.mode, sizeof(last_sent_fan_mode) - 1);
			} else if (strstr(item->topic, "stat/ap") != NULL) {
				last_ap_pub_tick = HAL_GetTick();
				pub_done_ap = true;
				last_sent_ap = dev_status.ap;
			} else if (strstr(item->topic, "stat/ac") != NULL) {
				last_ac_pub_tick = HAL_GetTick();
				pub_done_ac = true;
			} else if (strstr(item->topic, "stat/pb") != NULL) {
				uint32_t current_tick = HAL_GetTick();
				pub_done_pwr_all = true;
				last_pwr_all_pub_tick = current_tick;
				for (int i = 0; i < 8; i++) {
					pub_done_pwr[i] = true;
					last_sent_pwr[i].watt = dev_status.pwr_ch[i].watt;
				}
			}
		} else {
			printf("[MQTT TX] FAIL Topic: %s\r\n", item->topic);
		}
		g_defer_tail = (uint8_t)((g_defer_tail + 1) % MQTT_DEFER_MAX);
	}

	if (g_pending_action == MQTT_PENDING_DEV_RESET) {
		g_pending_action = MQTT_PENDING_NONE;
		osDelay(500);
		HAL_NVIC_SystemReset();
	} else if (g_pending_action == MQTT_PENDING_SET_NET) {
		g_pending_action = MQTT_PENDING_NONE;
		osDelay(200);
		NetConfig_SaveAndApply(&g_pending_net_cfg);
	} else if (g_pending_action == MQTT_PENDING_SET_BROKER) {
		g_pending_action = MQTT_PENDING_NONE;
		osDelay(200);
		NetConfig_SaveAndApply(&g_pending_net_cfg);
	} else if (g_pending_action == MQTT_PENDING_SET_NET_RESET) {
		g_pending_action = MQTT_PENDING_NONE;
		osDelay(200);
		NetConfig_SaveAndApplyEx(&g_pending_net_cfg, NET_SAVE_FACTORY_DHCP);
	}
}

void MqttAd_ProcessPending(void)
{
	MqttPending_Process();
}

static void mqtt_pb_build_stat(char *response, size_t resp_sz, bool is_valid)
{
	if (is_valid) {
		HAL_StatusTypeDef pb_st = PowerBoard_UpdateAllData(dev_status.pwr_ch);
		if (pb_st == HAL_OK) {
			dev_status.is_pwr_connected = true;
		} else {
			dev_status.is_pwr_connected = false;
		}
		uint8_t current_pb_id = (uint8_t)dev_status.pwr_ch[0].b_id;
		int len = snprintf(response, resp_sz, "{\"b_id\":%d,\"pb\":[", current_pb_id);
		for (int i = 0; i < 8; i++) {
			int s = (strcmp(dev_status.pwr_ch[i].pwr, "ON") == 0) ? 1 : 0;
			len += snprintf(response + len, resp_sz - len,
					"{\"ch\":%d,\"sw\":%d,\"c\":%.2f,\"w\":%.1f,\"v\":%d}%s",
					(i + 1), s, dev_status.pwr_ch[i].curr, dev_status.pwr_ch[i].watt,
					(int)dev_status.pwr_ch[i].volt, (i < 7) ? "," : "");
		}
		snprintf(response + len, resp_sz - len, "],\"conn\":%d,\"result\":1}",
			dev_status.is_pwr_connected ? 1 : 0);
	} else {
		uint8_t current_pb_id = (uint8_t)dev_status.pwr_ch[0].b_id;
		int conn = dev_status.is_pwr_connected ? 1 : 0;
		snprintf(response, resp_sz, "{\"b_id\":%d,\"pb\":0,\"conn\":%d,\"result\":0}", current_pb_id, conn);
	}
}

/* ── LG AC / Himpel AP MQTT payload 검증 (실내기·공청기 각 1대) ── */

static int mqtt_ac_mode_from_str(const char *mod, uint8_t *code)
{
	if (mod == NULL || code == NULL) {
		return -1;
	}
	if (strcmp(mod, "COOL") == 0) { *code = AC_MODE_COOL; return 0; }
	if (strcmp(mod, "DRY") == 0)  { *code = AC_MODE_DRY;  return 0; }
	if (strcmp(mod, "FAN") == 0)  { *code = AC_MODE_FAN;  return 0; }
	if (strcmp(mod, "AUTO") == 0) { *code = AC_MODE_AUTO; return 0; }
	if (strcmp(mod, "HEAT") == 0) { *code = AC_MODE_HEAT; return 0; }
	return -1;
}

static bool mqtt_ac_temp_c_valid(float temp_c)
{
	return (temp_c >= 16.0f && temp_c <= 30.0f);
}

static bool mqtt_ac_spd_valid(int spd)
{
	return (spd >= 1 && spd <= 6);
}

static bool mqtt_ap_mode_valid(const char *mode)
{
	if (mode == NULL) {
		return false;
	}
	return (strcmp(mode, "AUTO") == 0 || strcmp(mode, "MANUAL") == 0
			|| strcmp(mode, "CLEAN") == 0 || strcmp(mode, "VENT") == 0
			|| strcmp(mode, "BYPASS") == 0 || strcmp(mode, "HEATER") == 0);
}

static bool mqtt_ap_spd_valid(int spd)
{
	return (spd >= 0 && spd <= 4);
}

static void mqtt_build_ac_stat(char *response, size_t resp_sz, int result)
{
	snprintf(response, resp_sz,
			"{\"pwr\":%d,\"mode\":\"%s\",\"temp\":%.1f,\"curr\":%.1f,\"spd\":%d,\"err\":%d,\"conn\":%d,\"result\":%d}",
			(strcmp(dev_status.ac.pwr, "ON") == 0 ? 1 : 0), dev_status.ac.mode,
			(double)dev_status.ac.temp_sel, (double)dev_status.ac.temp_curr,
			dev_status.ac.fan_speed, dev_status.ac.error_code,
			dev_status.ac.is_connected ? 1 : 0, result);
}

static void mqtt_build_ap_stat(char *response, size_t resp_sz, int result)
{
	snprintf(response, resp_sz,
			"{\"pwr\":%d,\"mode\":\"%s\",\"spd\":%d,\"uv\":%d,\"co2\":%d,\"pm25\":%d,\"pm10\":%d,\"pm1_0\":%d,\"temp\":%.1f,\"temp_out\":%.1f,\"humi\":%d,\"tvoc\":%d,\"filter\":%d,\"conn\":%d,\"result\":%d}",
			(strcmp(dev_status.ap.pwr, "ON") == 0 ? 1 : 0), dev_status.ap.mode, dev_status.ap.fan_speed,
			dev_status.ap.uv_on ? 1 : 0, dev_status.ap.co2, dev_status.ap.dust_pm25, dev_status.ap.dust_pm10,
			dev_status.ap.dust_pm1_0, dev_status.ap.temp_in, dev_status.ap.temp_out, dev_status.ap.humi,
			dev_status.ap.tvoc, dev_status.ap.filter_life, dev_status.ap.is_connected ? 1 : 0, result);
}

static void mqtt_process_ac_pending(void)
{
	bool is_valid = false;
	bool is_query = g_hw_is_query;
	int modbus_ok = -1;
	char response_ac[512];
	const char *payload = g_hw_payload;

	if (!dev_status.ac.is_connected) {
		getACStatus();
	}

	if (strstr(payload, "\"set_ac\":") != NULL) {
		int p = -1, spd = -1;
		float tmpf = 0.0f;
		char mod[16] = "";
		uint8_t m_code = 0;
		if (sscanf(payload, "{\"set_ac\":{\"pwr\":%d,\"mode\":\"%15[^\"]\",\"temp\":%f,\"spd\":%d}}",
				&p, mod, &tmpf, &spd) == 4
				&& (p == 0 || p == 1)
				&& mqtt_ac_mode_from_str(mod, &m_code) == 0
				&& mqtt_ac_temp_c_valid(tmpf)
				&& mqtt_ac_spd_valid(spd)) {
			is_valid = true;
			modbus_ok = setACPower(p == 1);
			if (modbus_ok == 0) modbus_ok = setACMode(m_code);
			if (modbus_ok == 0) modbus_ok = setACTemperature((uint16_t)(tmpf * 10.0f + 0.5f));
			if (modbus_ok == 0) modbus_ok = setACWindSpeed((uint8_t)spd);
		}
	} else {
		bool any = false;
		modbus_ok = 0;
		const char *p_ptr = strstr(payload, "\"pwr\":");
		const char *m_ptr = strstr(payload, "\"mode\":");
		const char *t_ptr = strstr(payload, "\"temp\":");
		const char *s_ptr = strstr(payload, "\"spd\":");

		if (p_ptr != NULL) {
			int p;
			if (sscanf(p_ptr, "\"pwr\":%d", &p) == 1 && (p == 0 || p == 1)) {
				any = true;
				if (setACPower(p == 1) != 0) {
					modbus_ok = -1;
				}
			}
		}
		if (modbus_ok != -1 && m_ptr != NULL) {
			char mod[16] = "";
			uint8_t m_code = 0;
			if (sscanf(m_ptr, "\"mode\":\"%15[^\"]\"", mod) == 1
					&& mqtt_ac_mode_from_str(mod, &m_code) == 0) {
				any = true;
				if (setACMode(m_code) != 0) {
					modbus_ok = -1;
				}
			}
		}
		if (modbus_ok != -1 && t_ptr != NULL) {
			float tmpf;
			if (sscanf(t_ptr, "\"temp\":%f", &tmpf) == 1 && mqtt_ac_temp_c_valid(tmpf)) {
				any = true;
				if (setACTemperature((uint16_t)(tmpf * 10.0f + 0.5f)) != 0) {
					modbus_ok = -1;
				}
			}
		}
		if (modbus_ok != -1 && s_ptr != NULL) {
			int spd;
			if (sscanf(s_ptr, "\"spd\":%d", &spd) == 1 && mqtt_ac_spd_valid(spd)) {
				any = true;
				if (setACWindSpeed((uint8_t)spd) != 0) {
					modbus_ok = -1;
				}
			}
		}
		if (strstr(payload, "\"get_ac\":\"state\"") != NULL) {
			any = true;
			is_query = true;
		}
		is_valid = any;
	}

	if (is_valid) {
		getACStatus();
		int result = is_query ? (dev_status.ac.is_connected ? 1 : 0) : (modbus_ok == 0 ? 1 : 0);
		mqtt_build_ac_stat(response_ac, sizeof(response_ac), result);
		mqtt_publish_deferred(topics.stat_ac, response_ac);
	} else {
		snprintf(response_ac, sizeof(response_ac), "{\"ac\":0,\"conn\":%d,\"result\":0}", dev_status.ac.is_connected ? 1 : 0);
		mqtt_publish_deferred(topics.stat_ac, response_ac);
	}
}

static void mqtt_process_ap_pending(void)
{
	bool is_valid = false;
	bool is_query = g_hw_is_query;
	int rs485_ok = -1;
	char response_ap[512];
	const char *payload = g_hw_payload;

	if (!dev_status.ap.is_connected) {
		requestAPUpdate();
	}

	if (strstr(payload, "\"set_ap\":") != NULL) {
		int p, s, u, fr, bp, tm;
		char m[16] = { 0 };
		if (sscanf(payload, "{\"set_ap\":{\"pwr\":%d,\"mode\":\"%15[^\"]\",\"spd\":%d,\"uv\":%d,\"filter_reset\":%d,\"bypass\":%d,\"timer\":%d}}",
				&p, m, &s, &u, &fr, &bp, &tm) == 7
				&& (p == 0 || p == 1)
				&& mqtt_ap_mode_valid(m)
				&& mqtt_ap_spd_valid(s)
				&& (u == 0 || u == 1)
				&& (fr == 0 || fr == 1)
				&& (bp == 0 || bp == 1)
				&& tm >= 0 && tm <= 255) {
			is_valid = true;
			rs485_ok = setAPAll(p, m, s, u, fr, bp, tm);
		}
	} else {
		bool any = false;
		rs485_ok = 0;
		const char *p_ptr = strstr(payload, "\"pwr\":");
		const char *m_ptr = strstr(payload, "\"mode\":");
		const char *s_ptr = strstr(payload, "\"spd\":");
		const char *u_ptr = strstr(payload, "\"uv\":");
		const char *f_ptr = strstr(payload, "\"filter_reset\":");
		const char *b_ptr = strstr(payload, "\"bypass\":");
		const char *tm_ptr = strstr(payload, "\"timer\":");

		if (p_ptr != NULL) {
			int p;
			if (sscanf(p_ptr, "\"pwr\":%d", &p) == 1 && (p == 0 || p == 1)) {
				any = true;
				if (setAPPower(p) != 0) {
					rs485_ok = -1;
				}
			}
		}
		if (rs485_ok != -1 && m_ptr != NULL) {
			char m[16] = { 0 };
			if (sscanf(m_ptr, "\"mode\":\"%15[^\"]\"", m) == 1 && mqtt_ap_mode_valid(m)) {
				any = true;
				if (setAPMode(m) != 0) {
					rs485_ok = -1;
				}
			}
		}
		if (rs485_ok != -1 && s_ptr != NULL) {
			int s;
			if (sscanf(s_ptr, "\"spd\":%d", &s) == 1 && mqtt_ap_spd_valid(s)) {
				any = true;
				if (setAPSpeed(s) != 0) {
					rs485_ok = -1;
				}
			}
		}
		if (rs485_ok != -1 && u_ptr != NULL) {
			int u;
			if (sscanf(u_ptr, "\"uv\":%d", &u) == 1 && (u == 0 || u == 1)) {
				any = true;
				if (setAPUV(u) != 0) {
					rs485_ok = -1;
				}
			}
		}
		if (rs485_ok != -1 && f_ptr != NULL) {
			int f;
			if (sscanf(f_ptr, "\"filter_reset\":%d", &f) == 1 && (f == 0 || f == 1)) {
				any = true;
				if (setAPFilterReset(f) != 0) {
					rs485_ok = -1;
				}
			}
		}
		if (rs485_ok != -1 && b_ptr != NULL) {
			int b;
			if (sscanf(b_ptr, "\"bypass\":%d", &b) == 1 && (b == 0 || b == 1)) {
				any = true;
				if (setAPBypass(b) != 0) {
					rs485_ok = -1;
				}
			}
		}
		if (rs485_ok != -1 && tm_ptr != NULL) {
			int t;
			if (sscanf(tm_ptr, "\"timer\":%d", &t) == 1 && t >= 0 && t <= 255) {
				any = true;
				if (setAPTimer(t) != 0) {
					rs485_ok = -1;
				}
			}
		}
		if (strstr(payload, "\"get_ap\":\"state\"") != NULL) {
			any = true;
			is_query = true;
		}
		is_valid = any;
	}

	if (is_valid) {
		if (is_query) {
			rs485_ok = requestAPUpdate();
		} else {
			requestAPUpdate();
		}
		int result = is_query ? ((rs485_ok == 0 && dev_status.ap.is_connected) ? 1 : 0) : (rs485_ok == 0 ? 1 : 0);
		mqtt_build_ap_stat(response_ap, sizeof(response_ap), result);
		mqtt_publish_deferred(topics.stat_ap, response_ap);
	} else {
		snprintf(response_ap, sizeof(response_ap), "{\"ap\":0,\"conn\":%d,\"result\":0}", dev_status.ap.is_connected ? 1 : 0);
		mqtt_publish_deferred(topics.stat_ap, response_ap);
	}
}

void MqttPending_Process(void)
{
	if (g_ad_seq_pending) {
		g_ad_seq_pending = false;
		for (int i = 0; i < 15; i++) {
			bool target_state = (g_ad_seq_str[i] == '1');
			bool current_state = RLY_GetStatus((uint8_t)(i + 1));
			if (current_state != target_state) {
				RLY_SetStatus((uint8_t)(i + 1), target_state);
				osDelay(500);
			}
		}
		printf("[RX] Sequential Control Complete.\r\n");
		char response[640];
		mqtt_build_ad_stat(response, sizeof(response), true);
		mqtt_publish_deferred(topics.stat_ad, response);
	}

	if (g_hw_pending != MQTT_HW_NONE) {
		MqttHwPending job = g_hw_pending;
		g_hw_pending = MQTT_HW_NONE;
		char response[640];

		switch (job) {
		case MQTT_HW_AD_CH: {
			bool target_state = (g_hw_ad_val == 1);
			bool current_state = RLY_GetStatus((uint8_t)g_hw_ad_ch);
			if (current_state != target_state) {
				RLY_SetStatus((uint8_t)g_hw_ad_ch, target_state);
			}
			printf("[RX] relay ch%d -> %d\r\n", g_hw_ad_ch, g_hw_ad_val);
			mqtt_build_ad_stat(response, sizeof(response), true);
			mqtt_publish_deferred(topics.stat_ad, response);
			break;
		}
		case MQTT_HW_PB_SINGLE:
			PowerBoard_ControlSingle((uint8_t)g_hw_pb_id, (uint8_t)g_hw_pb_ch, (uint8_t)g_hw_pb_sw);
			mqtt_pb_build_stat(response, sizeof(response), true);
			mqtt_publish_deferred(topics.stat_pb, response);
			break;
		case MQTT_HW_PB_ALL: {
			uint8_t states[8];
			for (int i = 0; i < 8; i++) {
				states[i] = (g_hw_pb_set[i] == '1') ? 1 : 0;
			}
			PowerBoard_ControlAll((uint8_t)g_hw_pb_id, states);
			mqtt_pb_build_stat(response, sizeof(response), true);
			mqtt_publish_deferred(topics.stat_pb, response);
			break;
		}
		case MQTT_HW_PB_GET:
			mqtt_pb_build_stat(response, sizeof(response), true);
			mqtt_publish_deferred(topics.stat_pb, response);
			break;
		case MQTT_HW_FAN_SET:
			strncpy(dev_status.fan.mode, g_hw_fan_mode, sizeof(dev_status.fan.mode) - 1);
			dev_status.fan.duty_percent = g_hw_fan_duty;
			if (strcmp(g_hw_fan_mode, "MANUAL") == 0) {
				__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, g_hw_fan_duty * 10);
			}
			if (dev_status.fan.is_connected) {
				snprintf(response, sizeof(response), "{\"mode\":\"%s\",\"duty\":%d,\"conn\":1,\"result\":1}",
						dev_status.fan.mode, dev_status.fan.duty_percent);
			} else {
				snprintf(response, sizeof(response), "{\"fan\":0,\"conn\":0,\"result\":1}");
			}
			mqtt_publish_deferred(topics.stat_fan, response);
			break;
		case MQTT_HW_AC:
			mqtt_process_ac_pending();
			break;
		case MQTT_HW_AP:
			mqtt_process_ap_pending();
			break;
		default:
			break;
		}
	}
}

static void mqtt_build_ad_stat(char *response, size_t resp_sz, bool is_valid)
{
	char r_list[128] = { 0 };
	int r_off = 0;
	for (int i = 0; i < 15; i++) {
		r_off += snprintf(r_list + r_off, sizeof(r_list) - r_off, "%d%s",
				dev_status.relay.ch[i] ? 1 : 0, (i < 14) ? "," : "");
	}
	snprintf(response, resp_sz, "{\"relays\":[%s],\"conn\":1,\"result\":%d}", r_list, is_valid ? 1 : 0);
}

void MQTT_MessageArrived(MessageData *md) {
	MQTTMessage *message = md->message;
	char topic[TOPIC_SIZE];
	char payload[256];   // [★주소 참조 버그 완전 교정] 256바이트 정석 배열 복원
	char response[640];  // [★주소 참조 버그 완전 교정] 640바이트 정석 배열 복원

	// ⚠️ 배열 이름 자체가 주소이므로 & 기호 없이 정확히 배열명만 전달합니다.
	snprintf(topic, sizeof(topic), "%.*s", md->topicName->lenstring.len, md->topicName->lenstring.data);
	snprintf(payload, sizeof(payload), "%.*s", (int) message->payloadlen, (char*) message->payload);
	printf("\r\n[MQTT RX] Topic: %s,%s\r\n", topic, payload);

	/* ======================================================================
	 * 1. [AD] 릴레이 제어 (정석 메모리 주소 및 원본 osDelay 구조 완전 복구)
	 * ====================================================================== */
	if (strstr(topic, "cmnd") != NULL && strstr(topic, "ad") != NULL) {
		bool is_valid = false;

		// [CASE 1] 전체 셋팅 명령어: {"set_ad":"100000000000000"}
		if (strstr(payload, "\"set_ad\"")) {
			char ad_str[32] = { 0 };
			if (sscanf(payload, "{\"set_ad\":\"%15[0-1]\"", ad_str) == 1 && strlen(ad_str) == 15) {
				is_valid = true;
				strncpy(g_ad_seq_str, ad_str, 15);
				g_ad_seq_str[15] = '\0';
				g_ad_seq_pending = true;
				printf("[RX] set_ad queued for sequential control.\r\n");
			}
		}
		// [CASE 2] 개별 셋팅 명령어: {"ch":1, "relay_1":1}
		else if (strstr(payload, "\"ch\"")) {
			int ch = -1;
			int val = -1;
			char *ch_ptr = strstr(payload, "\"ch\"");
			char *rly_ptr = strstr(payload, "\"relay_");
			if (ch_ptr && rly_ptr) {
				sscanf(ch_ptr, "\"ch\"%*[^0-9]%d", &ch);
				sscanf(rly_ptr, "\"relay_%*d\"%*[^0-9]%d", &val);
				if (ch >= 1 && ch <= 15 && (val == 0 || val == 1)) {
					g_hw_ad_ch = ch;
					g_hw_ad_val = val;
					g_hw_pending = MQTT_HW_AD_CH;
					is_valid = true;
				}
			}
		}
		// [CASE 3] 전체 상태 가져오기 명령어: {"get_ad":"state"}
		else if (strstr(payload, "\"get_ad\"")) {
			is_valid = true;
			printf("[RX] Request Status: get_ad received.\r\n");
			mqtt_build_ad_stat(response, sizeof(response), is_valid);
			mqtt_publish_deferred(topics.stat_ad, response);
		}
		return;
	}
	/* ======================================================================
	 * 2. [FAN] 쿨러 팬 (구조체 변수 기반 & 이미지 규격 반영)
	 * ====================================================================== */
	else if (strstr(topic, "cmnd") != NULL && strstr(topic, "fan") != NULL) {
		bool is_valid = false;

		// [CASE 1] 제어 명령 처리
		if (strstr(payload, "\"set_fan\"") != NULL) {
			char mod[12] = { 0 };
			int fan_duty = -1;
			if (sscanf(payload, "{\"set_fan\":{\"mode\":\"%11[^\"]\",\"duty\":%d}}", mod, &fan_duty) >= 2) {
				if (fan_duty >= 0 && fan_duty <= 100) {
					strncpy(g_hw_fan_mode, mod, sizeof(g_hw_fan_mode) - 1);
					g_hw_fan_duty = fan_duty;
					g_hw_pending = MQTT_HW_FAN_SET;
					is_valid = true;
				}
			}
		}
		// [CASE 2] 상태 요청 처리
		else if (strstr(topic, "/get") != NULL || strstr(payload, "\"info\":\"GET\"") != NULL || strstr(payload, "\"get_fan\":\"state\"") != NULL) {
			is_valid = true;
			if (dev_status.fan.is_connected) {
				snprintf(response, sizeof(response), "{\"mode\":\"%s\",\"duty\":%d,\"conn\":1,\"result\":%d}", dev_status.fan.mode, dev_status.fan.duty_percent, is_valid ? 1 : 0);
			} else {
				snprintf(response, sizeof(response), "{\"fan\":0,\"conn\":0,\"result\":1}");
			}
			mqtt_publish_deferred(topics.stat_fan, response);
		}
		return;
	}
	/* ======================================================================
	 * 3. [PB] 전력보드 제어 (구조체 변수 기반 & 설계서 규격 완벽 반영)
	 * ====================================================================== */
	else if (strstr(topic, "cmnd") != NULL && strstr(topic, "pb") != NULL) {
		uint8_t current_pb_id = (uint8_t)dev_status.pwr_ch[0].b_id;
		if (!dev_status.is_pwr_connected) {
			snprintf(response, sizeof(response), "{\"b_id\":%d,\"pb\":0,\"conn\":0,\"result\":0}", current_pb_id);
			mqtt_publish_deferred(topics.stat_pb, response);
			return;
		}

		bool is_valid = false;
		int tid = 0;
		int tch = -1;
		int sw = -1;
		char pwr_str[24] = { 0 };

		// A. [개별 채널 제어]
		if (sscanf(payload, "{\"b_id\":%d,\"ch\":%d,\"sw\":%d}", &tid, &tch, &sw) == 3 && tid == current_pb_id && tch >= 1 && tch <= 8 && (sw == 0 || sw == 1)) {
			g_hw_pb_id = tid;
			g_hw_pb_ch = tch;
			g_hw_pb_sw = sw;
			g_hw_pending = MQTT_HW_PB_SINGLE;
			is_valid = true;
		}
		// B. [8ch 전체 채널 제어]
		else if (strstr(payload, "set_pb")) {
			int res = sscanf(payload, "{\"b_id\":%d,\"set_pb\":\"%23[^\"]\"}", &tid, pwr_str);
			if (res != 2) {
				res = sscanf(payload, "{\"id\":%d,\"set_pb\":\"%23[^\"]\"}", &tid, pwr_str);
			}
			if (res == 2 && tid == current_pb_id && strlen(pwr_str) == 8) {
				bool err = false;
				for (int i = 0; i < 8; i++) {
					if (pwr_str[i] != '0' && pwr_str[i] != '1') {
						err = true;
						break;
					}
				}
				if (!err) {
					g_hw_pb_id = tid;
					strncpy(g_hw_pb_set, pwr_str, 8);
					g_hw_pb_set[8] = '\0';
					g_hw_pending = MQTT_HW_PB_ALL;
					is_valid = true;
				}
			}
		}
		// C. [전체 상태 요청 제어]
		else if (strstr(payload, "get_pb")) {
			if (sscanf(payload, "{\"b_id\":%d,\"get_pb\":\"state\"}", &tid) == 1 || sscanf(payload, "{\"id\":%d,\"get_pb\":\"state\"}", &tid) == 1) {
				if (tid == current_pb_id) {
					g_hw_pb_id = tid;
					g_hw_pending = MQTT_HW_PB_GET;
					is_valid = true;
				}
			}
		}

		if (!is_valid) {
			int conn = dev_status.is_pwr_connected ? 1 : 0;
			snprintf(response, sizeof(response), "{\"b_id\":%d,\"pb\":0,\"conn\":%d,\"result\":0}", current_pb_id, conn);
			mqtt_publish_deferred(topics.stat_pb, response);
		}
		return;
	}
	/* ======================================================================
	 * 4. [AP] Himpel 공기청정기 (개별/전체 제어 및 이미지 규격 반영)
	 * ====================================================================== */
	else if (strstr(topic, "cmnd") != NULL && strstr(topic, "ap") != NULL) {
		bool is_valid = false;
		bool is_query = false;
		char response_ap[512];

		if (strcmp(topic, topics.cmnd_ap_setall) == 0 || strstr(payload, "\"set_ap\":") != NULL) {
			int p, s, u, fr, bp, tm;
			char m[16] = { 0 };
			if (sscanf(payload, "{\"set_ap\":{\"pwr\":%d,\"mode\":\"%15[^\"]\",\"spd\":%d,\"uv\":%d,\"filter_reset\":%d,\"bypass\":%d,\"timer\":%d}}", &p, m, &s, &u, &fr, &bp, &tm) == 7) {
				is_valid = true;
			}
		}
		else if (strstr(payload, "\"get_ap\":\"state\"") != NULL || strcmp(topic, topics.cmnd_ap_getall) == 0) {
			is_valid = true;
			is_query = true;
		}
		else if (strstr(payload, "\"pwr\":") || strstr(payload, "\"mode\":") || strstr(payload, "\"spd\":")
				|| strstr(payload, "\"uv\":") || strstr(payload, "\"filter_reset\":")
				|| strstr(payload, "\"bypass\":") || strstr(payload, "\"timer\":")) {
			is_valid = true;
		}

		if (is_valid) {
			strncpy(g_hw_payload, payload, sizeof(g_hw_payload) - 1);
			g_hw_payload[sizeof(g_hw_payload) - 1] = '\0';
			g_hw_is_query = is_query;
			g_hw_pending = MQTT_HW_AP;
		} else {
			snprintf(response_ap, sizeof(response_ap), "{\"ap\":0,\"conn\":%d,\"result\":0}", dev_status.ap.is_connected ? 1 : 0);
			mqtt_publish_deferred(topics.stat_ap, response_ap);
		}
		return;
	}
	/* ======================================================================
	 * 5. [AC] LG 에어컨 (구조체 변수 기반 & 개별/통합 제어 최종 반영)
	 * ====================================================================== */
	else if (strstr(topic, "cmnd") != NULL && strstr(topic, "ac") != NULL) {
		bool is_valid = false;
		bool is_query = false;
		char response_ac[512];

		if (strcmp(topic, topics.cmnd_ac_setall) == 0 || strstr(payload, "\"set_ac\":") != NULL) {
			int p = -1, tmp = -1, spd = -1;
			char mod[16] = "";
			if (sscanf(payload, "{\"set_ac\":{\"pwr\":%d,\"mode\":\"%15[^\"]\",\"temp\":%d,\"spd\":%d}}", &p, mod, &tmp, &spd) == 4) {
				is_valid = true;
			}
		}
		else if (strstr(payload, "\"get_ac\":\"state\"") != NULL || strcmp(topic, topics.cmnd_ac_getall) == 0) {
			is_valid = true;
			is_query = true;
		}
		else if (strstr(payload, "\"pwr\":") || strstr(payload, "\"mode\":")
				|| strstr(payload, "\"temp\":") || strstr(payload, "\"spd\":")) {
			is_valid = true;
		}

		if (is_valid) {
			strncpy(g_hw_payload, payload, sizeof(g_hw_payload) - 1);
			g_hw_payload[sizeof(g_hw_payload) - 1] = '\0';
			g_hw_is_query = is_query;
			g_hw_pending = MQTT_HW_AC;
		} else {
			snprintf(response_ac, sizeof(response_ac), "{\"ac\":0,\"conn\":%d,\"result\":0}", dev_status.ac.is_connected ? 1 : 0);
			mqtt_publish_deferred(topics.stat_ac, response_ac);
		}
		return;
	}
	/* ======================================================================
	 * 6. [SYS] 시스템 제어 (이미지 규격: ALL_DATA, DEV_RESET 최종 반영)
	 * ====================================================================== */
	else if (strstr(topic, topics.cmnd_dev)) {
		char *p_buf = (char*) malloc(1536);
		if (p_buf == NULL) {
			return;
		}

		// [CASE 1] 장치 물리 리셋 시퀀스
		if (strstr(payload, "\"reset\":\"DEV_RESET\"") != NULL) {
			snprintf(p_buf, 1536, "{\"device\":\"DEV_RESET\",\"conn\":0,\"result\":1}");
			mqtt_publish_deferred(topics.stat_dev, p_buf);
			g_pending_action = MQTT_PENDING_DEV_RESET;
			free(p_buf);
			return;
		}
		// [CASE 2] 모든 데이터 가져오기 일괄 보고 (ALL_DATA)
		else if (strstr(payload, "\"data\":\"ALL_DATA\"") != NULL) {
			Sensor_RefreshEnvironmentCache();
			if (PowerBoard_UpdateAllData(dev_status.pwr_ch) == HAL_OK) {
				dev_status.is_pwr_connected = true;
			} else {
				dev_status.is_pwr_connected = false;
			}

			uint32_t now = HAL_GetTick();
			char cur_time[24];
			Get_Current_Time_Log_Str(cur_time, sizeof(cur_time));
			printf("[Report_time] : %s\r\n", cur_time);
			printf("[ALL_DATA] tele burst: dust, th_in, th_out, ad, fan, ac, pb, ap, input\r\n");

			snprintf(p_buf, 1536, "{\"device\":\"ALL_DATA\",\"conn\":1,\"result\":1}");
			mqtt_publish_deferred(topics.stat_dev, p_buf);

			snprintf(p_buf, 1536, "{\"temp\":%.1f,\"humi\":%.1f,\"conn\":%d}", dev_status.th_in.temp, dev_status.th_in.humi, dev_status.th_in.is_connected ? 1 : 0);
			mqtt_publish_deferred(topics.tele_th_in, p_buf);

			snprintf(p_buf, 1536, "{\"temp\":%.1f,\"humi\":%.1f,\"conn\":%d}", dev_status.th_out.temp, dev_status.th_out.humi, dev_status.th_out.is_connected ? 1 : 0);
			mqtt_publish_deferred(topics.tele_th_out, p_buf);

			snprintf(p_buf, 1536, "{\"pm1_0\":%d,\"pm2_5\":%d,\"pm10\":%d,\"conn\":%d}", dev_status.dust.pm1_0, dev_status.dust.pm2_5, dev_status.dust.pm10, dev_status.dust.is_connected ? 1 : 0);
			mqtt_publish_deferred(topics.tele_dust, p_buf);

			char r_list[128] = { 0 };
			int r_off = 0;
			for (int i = 0; i < 15; i++) {
				r_off += snprintf(r_list + r_off, sizeof(r_list) - r_off, "%d%s", dev_status.relay.ch[i] ? 1 : 0, (i < 14) ? "," : "");
			}
			snprintf(p_buf, 1536, "{\"relays\":[%s],\"conn\":1}", r_list);
			mqtt_publish_deferred(topics.tele_ad, p_buf);

			snprintf(p_buf, 1536, "{\"mode\":\"%s\",\"duty\":%d,\"rpm\":%d,\"conn\":%d}", dev_status.fan.mode, dev_status.fan.duty_percent, dev_status.fan.rpm, dev_status.fan.is_connected ? 1 : 0);
			mqtt_publish_deferred(topics.tele_fan, p_buf);

			snprintf(p_buf, 1536, "{\"pwr\":%d,\"mode\":\"%s\",\"temp\":%.1f,\"curr\":%.1f,\"spd\":%d,\"err\":%d,\"conn\":%d}", (strcmp(dev_status.ac.pwr, "ON") == 0 ? 1 : 0), dev_status.ac.mode, (double)dev_status.ac.temp_sel, (double)dev_status.ac.temp_curr, dev_status.ac.fan_speed, dev_status.ac.error_code, dev_status.ac.is_connected ? 1 : 0);
			mqtt_publish_deferred(topics.tele_ac, p_buf);

			uint8_t pb_id = (uint8_t)dev_status.pwr_ch[0].b_id;
			if (dev_status.is_pwr_connected) {
				int pb_len = snprintf(p_buf, 1536, "{\"b_id\":%d,\"pb\":[", pb_id);
				for (int i = 0; i < 8; i++) {
					int sw_stat = (strcmp(dev_status.pwr_ch[i].pwr, "ON") == 0) ? 1 : 0;
					pb_len += snprintf(p_buf + pb_len, 1536 - pb_len, "{\"ch\":%d,\"sw\":%d,\"c\":%.2f,\"w\":%.1f,\"v\":%d}%s", (i + 1), sw_stat, dev_status.pwr_ch[i].curr, dev_status.pwr_ch[i].watt, (int)dev_status.pwr_ch[i].volt, (i < 7) ? "," : "");
				}
				snprintf(p_buf + pb_len, 1536 - pb_len, "],\"conn\":1}");
			} else {
				snprintf(p_buf, 1536, "{\"b_id\":%d,\"pb\":0,\"conn\":0}", pb_id);
			}
			mqtt_publish_deferred(topics.tele_pb, p_buf);

			snprintf(p_buf, 1024, "{\"pwr\":%d,\"mode\":\"%s\",\"spd\":%d,\"uv\":%d,\"co2\":%d,\"pm25\":%d,\"pm10\":%d,\"pm1_0\":%d,\"temp\":%.1f,\"temp_out\":%.1f,\"humi\":%d,\"tvoc\":%d,\"filter\":%d,\"conn\":%d}", !strcmp(dev_status.ap.pwr, "ON"), dev_status.ap.mode, dev_status.ap.fan_speed, dev_status.ap.uv_on, dev_status.ap.co2, dev_status.ap.dust_pm25, dev_status.ap.dust_pm10, dev_status.ap.dust_pm1_0, dev_status.ap.temp_in, dev_status.ap.temp_out, dev_status.ap.humi, dev_status.ap.tvoc, dev_status.ap.filter_life, dev_status.ap.is_connected ? 1 : 0);
			mqtt_publish_deferred(topics.tele_ap, p_buf);

			snprintf(p_buf, 1536, "{\"in\":[%d,%d,%d,%d],\"count\":[%lu,%lu,%lu,%lu],\"conn\":1}", dev_status.in_stat.p4, dev_status.in_stat.p5, dev_status.in_stat.p6, dev_status.in_stat.p7, (unsigned long)dev_status.in_stat.cnt4, (unsigned long)dev_status.in_stat.cnt5, (unsigned long)dev_status.in_stat.cnt6, (unsigned long)dev_status.in_stat.cnt7);
			mqtt_publish_deferred(topics.tele_input, p_buf);

			last_th_in_pub_tick = now; last_th_out_pub_tick = now; last_dust_pub_tick = now; last_ac_pub_tick = now; last_fan_pub_tick = now; last_pwr_all_pub_tick = now; last_ap_pub_tick = now; last_input_pub_tick = now;
			pub_done_pwr_all = true; pub_done_input = true; pub_done_relay = true;
			free(p_buf);
			return;
		}
		// [CASE 3] 제어보드 고정 IP 주소 설정 및 STATIC 모드 영구 전환
		else if (strstr(payload, "\"set_net\"") != NULL) {
			NetRuntimeConfig new_cfg = g_net_cfg;
			int ip[4] = {0}, sn[4] = {0}, gw[4] = {0}, dns[4] = {0};
			bool ok = false;
			if (strstr(payload, "\"ip\"") && sscanf(strstr(payload, "\"ip\""), "\"ip\":[%d,%d,%d,%d]", &ip[0], &ip[1], &ip[2], &ip[3]) == 4 &&
				strstr(payload, "\"sn\"") && sscanf(strstr(payload, "\"sn\""), "\"sn\":[%d,%d,%d,%d]", &sn[0], &sn[1], &sn[2], &sn[3]) == 4 &&
				strstr(payload, "\"gw\"") && sscanf(strstr(payload, "\"gw\""), "\"gw\":[%d,%d,%d,%d]", &gw[0], &gw[1], &gw[2], &gw[3]) == 4) {
				for (int i = 0; i < 4; i++) {
					new_cfg.ip[i] = (uint8_t)ip[i];
					new_cfg.sn[i] = (uint8_t)sn[i];
					new_cfg.gw[i] = (uint8_t)gw[i];
				}
				if (strstr(payload, "\"dns\"") && sscanf(strstr(payload, "\"dns\""), "\"dns\":[%d,%d,%d,%d]", &dns[0], &dns[1], &dns[2], &dns[3]) == 4) {
					for (int i = 0; i < 4; i++) new_cfg.dns[i] = (uint8_t)dns[i];
				}
				new_cfg.net_mode = 0;
				ok = true;
			}
			if (ok) {
				snprintf(p_buf, 1536, "{\"device\":\"SET_NET\",\"conn\":1,\"result\":1}");
				mqtt_publish_deferred(topics.stat_dev, p_buf);
				g_pending_net_cfg = new_cfg;
				g_pending_action = MQTT_PENDING_SET_NET;
			} else {
				snprintf(p_buf, 1536, "{\"device\":\"SET_NET\",\"conn\":1,\"result\":0}");
				mqtt_publish_deferred(topics.stat_dev, p_buf);
			}
			free(p_buf);
			return;
		}
		// [CASE 4] MQTT 브로커 PC의 IP 및 포트 정보 변경
		else if (strstr(payload, "\"set_broker\"") != NULL) {
			NetRuntimeConfig new_cfg = g_net_cfg;
			int ip[4] = {0}, port = 0;
			bool ok = false;
			if (strstr(payload, "\"ip\"") && sscanf(strstr(payload, "\"ip\""), "\"ip\":[%d,%d,%d,%d]", &ip[0], &ip[1], &ip[2], &ip[3]) == 4) {
				for (int i = 0; i < 4; i++) new_cfg.broker_ip[i] = (uint8_t)ip[i];
				ok = true;
			}
			if (strstr(payload, "\"port\"") && sscanf(strstr(payload, "\"port\""), "\"port\":%d", &port) == 1 && port > 0 && port <= 65535) {
				new_cfg.broker_port = (uint16_t)port;
			}
			if (ok) {
				snprintf(p_buf, 1536, "{\"device\":\"SET_BROKER\",\"conn\":1,\"result\":1}");
				mqtt_publish_deferred(topics.stat_dev, p_buf);
				g_pending_net_cfg = new_cfg;
				g_pending_action = MQTT_PENDING_SET_BROKER;
			} else {
				snprintf(p_buf, 1536, "{\"device\":\"SET_BROKER\",\"conn\":1,\"result\":0}");
				mqtt_publish_deferred(topics.stat_dev, p_buf);
			}
			free(p_buf);
			return;
		}
		// [CASE 5] 원격 강제 DHCP 원상 복구
		else if (strstr(payload, "\"set_net_reset\"") != NULL) {
			NetRuntimeConfig new_cfg;
			memset(&new_cfg, 0, sizeof(new_cfg));
			new_cfg.net_mode = 1;
			memcpy(new_cfg.broker_ip, g_net_cfg.broker_ip, 4);
			new_cfg.broker_port = g_net_cfg.broker_port;
			snprintf(p_buf, 1536, "{\"device\":\"SET_NET_RESET\",\"conn\":1,\"result\":1}");
			mqtt_publish_deferred(topics.stat_dev, p_buf);
			g_pending_net_cfg = new_cfg;
			g_pending_action = MQTT_PENDING_SET_NET_RESET;
			free(p_buf);
			return;
		}
		free(p_buf);
		return;
	}
	/* ======================================================================
	 * 7. [INPUT] 외부 입력 4채널 조회/카운트 초기화 (v2.0 신규)
	 * ====================================================================== */
	else if (strstr(topic, topics.cmnd_input) != NULL) {
		char resp[256];
		if (strstr(payload, "\"get_input\"") != NULL) {
			SCAN_External_Inputs();
			printf("[RX] Request Status: get_input received.\r\n");
			snprintf(resp, sizeof(resp), "{\"in\":[%d,%d,%d,%d],\"count\":[%lu,%lu,%lu,%lu],\"conn\":1,\"result\":1}", dev_status.in_stat.p4, dev_status.in_stat.p5, dev_status.in_stat.p6, dev_status.in_stat.p7, (unsigned long) dev_status.in_stat.cnt4, (unsigned long) dev_status.in_stat.cnt5, (unsigned long) dev_status.in_stat.cnt6, (unsigned long) dev_status.in_stat.cnt7);
			mqtt_publish_deferred(topics.stat_input, resp);
		} else if (strstr(payload, "\"reset_count\"") != NULL) {
			dev_status.in_stat.cnt4 = dev_status.in_stat.cnt5 = dev_status.in_stat.cnt6 = dev_status.in_stat.cnt7 = 0;
			snprintf(resp, sizeof(resp), "{\"in\":[%d,%d,%d,%d],\"count\":[0,0,0,0],\"conn\":1,\"result\":1}", dev_status.in_stat.p4, dev_status.in_stat.p5, dev_status.in_stat.p6, dev_status.in_stat.p7);
			mqtt_publish_deferred(topics.stat_input, resp);
		}
		return;
	}
}
