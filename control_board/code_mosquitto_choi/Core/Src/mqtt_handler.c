#include "mqtt_handler.h"
#include "app.h"
#include "power_board.h"
#include "himpel.h"
#include "app_loop.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

extern void getAPStatusAll(APData* dest);
extern void NetConfig_ExecuteFlashEraseAndReboot(void); // ◀ 이 라인을 헤더 밑에 추가해 주세요!


void MQTT_MessageArrived(MessageData *md) {
	MQTTMessage *message = md->message;
	char topic[TOPIC_SIZE];
	char payload[256];   // PDF Page 1 규격 원본 배열 크기 원상 복구
	char response[640];  // PDF Page 1 규격 640바이트 버퍼 크기 완벽 고정

	snprintf(topic, sizeof(topic), "%.*s", md->topicName->lenstring.len,
			md->topicName->lenstring.data);
	snprintf(payload, sizeof(payload), "%.*s", (int) message->payloadlen,
			(char*) message->payload);
	printf("\r\n[MQTT RX] Topic: %s,%s\r\n", topic, payload);

	/* ======================================================================
	 * 1. [AD] 릴레이 제어 (구조체 변수 기반 & 이미지 규격 반영)
	 * ====================================================================== */
	if (strstr(topic, "cmnd") != NULL && strstr(topic, "ad") != NULL) {
		bool is_valid = false;

		// [CASE 1] 전체 셋팅 명령어: {"set_ad":"100000000000000"}
		if (strstr(payload, "\"set_ad\"")) {
			char ad_str[32] = { 0 }; // PDF Page 1 원본 배열 형식 완전 복구
			if (sscanf(payload, "{\"set_ad\":\"%15[0-1]\"", ad_str) == 1
					&& strlen(ad_str) == 15) {
				is_valid = true;
				for (int i = 0; i < 15; i++) {
					bool target_state = (ad_str[i] == '1');
					bool current_state = RLY_GetStatus(i + 1);
					if (current_state != target_state) {
						RLY_SetStatus(i + 1, target_state);
						osDelay(500); // 500ms 순차 전원 보호 지연
					}
				}
				printf("[RX] Sequential Control Complete.\r\n");
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
					bool target_state = (val == 1);
					bool current_state = RLY_GetStatus(ch);
					if (current_state != target_state) {
						RLY_SetStatus(ch, target_state);
					}
					is_valid = true;
				}
			}
		}
		// [CASE 3] 전체 상태 가져오기 명령어: {"get_ad":"state"}
		else if (strstr(payload, "\"get_ad\"")) {
			is_valid = true;
			printf("[RX] Request Status: get_ad received.\r\n");
		}

		/* ── [Phase 3] 응답 프로토콜 조립 및 전송 ── */
		char r_list[128] = { 0 }; // PDF Page 2 원본 128바이트 고정 배열 복원
		int r_off = 0;
		for (int i = 0; i < 15; i++) {
			r_off += snprintf(r_list + r_off, sizeof(r_list) - r_off, "%d%s",
					dev_status.relay.ch[i] ? 1 : 0, (i < 14) ? "," : "");
		}
		snprintf(response, sizeof(response),
				"{\"relays\":[%s],\"conn\":1,\"result\":%d}", r_list,
				is_valid ? 1 : 0);

		// 원본 인라인 구조체 전송부 유지
		if (MQTTPublish(&c, topics.stat_ad, &(MQTTMessage ) { QOS0, 0, 0, 0,
						response, (int) strlen(response) }) == SUCCESS) {
			pub_done_relay = true;
			last_state_pub_tick = HAL_GetTick();
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
			char mod[12] = { 0 }; // PDF Page 3 원본 12바이트 배열 완전 복구
			int fan_duty = -1;
			if (sscanf(payload,
					"{\"set_fan\":{\"mode\":\"%11[^\"]\",\"duty\":%d}}", mod,
					&fan_duty) >= 2) {
				if (fan_duty >= 0 && fan_duty <= 100) {
					strncpy(dev_status.fan.mode, mod,
							sizeof(dev_status.fan.mode) - 1);
					dev_status.fan.duty_percent = fan_duty;
					if (strcmp(mod, "MANUAL") == 0) {
						__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2,
								fan_duty * 10);
					}
					is_valid = true;
				}
			}
		}
		// [CASE 2] 상태 요청 처리
		else if (strstr(topic, "/get") != NULL
				|| strstr(payload, "\"info\":\"GET\"") != NULL
				|| strstr(payload, "\"get_fan\":\"state\"") != NULL) {
			is_valid = true;
		}

		/* --- [응답 생성: 이미지 규격 반영] --- */
		if (dev_status.fan.is_connected) {
			snprintf(response, sizeof(response),
					"{\"mode\":\"%s\",\"duty\":%d,\"conn\":1,\"result\":%d}",
					dev_status.fan.mode, dev_status.fan.duty_percent,
					is_valid ? 1 : 0);
		} else {
			snprintf(response, sizeof(response),
					"{\"fan\":0,\"conn\":0,\"result\":1}");
		}

		if (MQTTPublish(&c, topics.stat_fan, &(MQTTMessage ) { QOS0, 0, 0, 0,
						response, (int) strlen(response) }) == SUCCESS) {
			pub_done_fan = true;
			last_fan_pub_tick = HAL_GetTick();
			last_sent_fan_duty = dev_status.fan.duty_percent;
			strncpy(last_sent_fan_mode, dev_status.fan.mode,
					sizeof(last_sent_fan_mode) - 1);
		}
		return;
	}
	/* ======================================================================
	 * 3. [PB] 전력보드 제어 (구조체 변수 기반 & 설계서 규격 완벽 반영)
	 * ====================================================================== */
	else if (strstr(topic, "cmnd") != NULL && strstr(topic, "pb") != NULL) {
		uint8_t current_pb_id = (uint8_t) dev_status.pwr_ch[0].b_id; // PDF Page 5 배열 포인터 인덱스 참조 복원
		if (!dev_status.is_pwr_connected) {
			snprintf(response, sizeof(response),
					"{\"b_id\":%d,\"pb\":0,\"conn\":1,\"result\":0}",
					current_pb_id);
			MQTTPublish(&c, topics.stat_pb, &(MQTTMessage ) { QOS0, 0, 0, 0,
							response, (int) strlen(response) });
			return;
		}

		bool is_valid = false;
		int tid = 0;
		int tch = -1;
		int sw = -1;
		char pwr_str[24] = { 0 }; // PDF Page 5 원본 24바이트 배열 완전 복구

		// A. [개별 채널 제어]
		if (sscanf(payload, "{\"b_id\":%d,\"ch\":%d,\"sw\":%d}", &tid, &tch,
				&sw) == 3 && tid == current_pb_id && tch >= 1 && tch <= 8
				&& (sw == 0 || sw == 1)) {
			PowerBoard_ControlSingle((uint8_t) tid, (uint8_t) tch,
					(uint8_t) sw);
			is_valid = true;
		}
		// B. [8ch 전체 채널 제어]
		else if (strstr(payload, "set_pb")) {
			int res = sscanf(payload, "{\"b_id\":%d,\"set_pb\":\"%23[^\"]\"}",
					&tid, pwr_str);
			if (res != 2) {
				res = sscanf(payload, "{\"id\":%d,\"set_pb\":\"%23[^\"]\"}",
						&tid, pwr_str);
			}
			if (res == 2 && tid == current_pb_id && strlen(pwr_str) == 8) {
				uint8_t states[8]; // PDF Page 5 규격 원본 스택 배열 형태 복원
				bool err = false;
				for (int i = 0; i < 8; i++) {
					if (pwr_str[i] != '0' && pwr_str[i] != '1') {
						err = true;
						break;
					}
					states[i] = (pwr_str[i] == '1');
				}
				if (!err) {
					PowerBoard_ControlAll((uint8_t) tid, states);
					is_valid = true;
				}
			}
		}
		// C. [전체 상태 요청 제어]
		else if (strstr(payload, "get_pb")) {
			if (sscanf(payload, "{\"b_id\":%d,\"get_pb\":\"state\"}", &tid) == 1
					|| sscanf(payload, "{\"id\":%d,\"get_pb\":\"state\"}", &tid)
							== 1) {
				if (tid == current_pb_id) {
					is_valid = true;
				}
			}
		}

		// --- [응답 생성: 전류 'c' 추가 및 전체 중괄호 보정] ---
		if (is_valid) {
			PowerBoard_UpdateAllData(dev_status.pwr_ch);
			int len = snprintf(response, sizeof(response),
					"{\"b_id\":%d,\"pb\":[", current_pb_id);
			for (int i = 0; i < 8; i++) {
				int s = (strcmp(dev_status.pwr_ch[i].pwr, "ON") == 0) ? 1 : 0;
				len +=
						snprintf(response + len, sizeof(response) - len,
								"{\"ch\":%d,\"sw\":%d,\"c\":%.2f,\"w\":%.1f,\"v\":%d}%s",
								(i + 1), s, dev_status.pwr_ch[i].curr,
								dev_status.pwr_ch[i].watt,
								(int) dev_status.pwr_ch[i].volt,
								(i < 7) ? "," : "");
			}
			snprintf(response + len, sizeof(response) - len,
					"],\"conn\":1,\"result\":1}");
		} else {
			snprintf(response, sizeof(response),
					"{\"b_id\":%d,\"pb\":0,\"conn\":1,\"result\":0}",
					current_pb_id);
		}

		if (MQTTPublish(&c, topics.stat_pb, &(MQTTMessage ) { QOS0, 0, 0, 0,
						response, (int) strlen(response) }) == SUCCESS) {
			uint32_t current_tick = HAL_GetTick();
			pub_done_pwr_all = true;
			last_pwr_all_pub_tick = current_tick;
			for (int i = 0; i < 8; i++) {
				pub_done_pwr[i] = true;
				last_sent_pwr[i].watt = dev_status.pwr_ch[i].watt;
			}
		}
		return;
	}
	/* ======================================================================
	 * 4. [AP] Himpel 공기청정기 (개별/전체 제어 및 이미지 규격 반영)
	 * ====================================================================== */
	else if (strstr(topic, "cmnd") != NULL && strstr(topic, "ap") != NULL) {
		bool is_valid = false;
		bool is_query = false;
		int rs485_ok = -1;
		char response_ap[512]; // PDF Page 7 원본 로컬 배열 스페이스 완전 준수

		// [1. 연결 상태 체크] conn:0 이면 1회 RS485 탐색 후 재판정
		if (!dev_status.ap.is_connected) {
			requestAPUpdate();
			if (!dev_status.ap.is_connected) {
				snprintf(response_ap, sizeof(response_ap),
						"{\"ap\":0,\"conn\":0,\"result\":0}");
				MQTTPublish(&c, topics.stat_ap, &(MQTTMessage ) { QOS0, 0, 0, 0,
								response_ap, (int) strlen(response_ap) });
				return;
			}
		}

		// --- [CASE 1: 전체 설정 (set_ap)] ---
		if (strcmp(topic, topics.cmnd_ap_setall)
				== 0|| strstr(payload, "\"set_ap\":") != NULL) {
			int p, s, u, fr, bp, tm;
			char m[16] = { 0 }; // PDF Page 8 규격 이스케이프 및 배열 크기 완전 복원
			if (sscanf(payload,
					"{\"set_ap\":{\"pwr\":%d,\"mode\":\"%15[^\"]\",\"spd\":%d,\"uv\":%d,\"filter_reset\":%d,\"bypass\":%d,\"timer\":%d}}",
					&p, m, &s, &u, &fr, &bp, &tm) == 7) {
				is_valid = true;
				rs485_ok = setAPAll(p, m, s, u, fr, bp, tm);
			}
		}
		// --- [CASE 2: 개별 제어 7종 이스케이프 완벽 교정] ---
		else if (strstr(payload, "\"pwr\":")) {
			int p;
			if (sscanf(payload, "{\"pwr\":%d}", &p) == 1) {
				is_valid = true;
				rs485_ok = setAPPower(p);
			}
		} else if (strstr(payload, "\"mode\":")) {
			char m[16] = { 0 }; // PDF Page 8 복원
			if (sscanf(payload, "{\"mode\":\"%15[^\"]\"}", m) == 1) {
				is_valid = true;
				rs485_ok = setAPMode(m);
			}
		} else if (strstr(payload, "\"spd\":")) {
			int s;
			if (sscanf(payload, "{\"spd\":%d}", &s) == 1) {
				is_valid = true;
				rs485_ok = setAPSpeed(s);
			}
		} else if (strstr(payload, "\"uv\":")) {
			int u;
			if (sscanf(payload, "{\"uv\":%d}", &u) == 1) {
				is_valid = true;
				rs485_ok = setAPUV(u);
			}
		} else if (strstr(payload, "\"filter_reset\":")) {
			int f;
			if (sscanf(payload, "{\"filter_reset\":%d}", &f) == 1) {
				is_valid = true;
				rs485_ok = setAPFilterReset(f);
			}
		} else if (strstr(payload, "\"bypass\":")) {
			int b;
			if (sscanf(payload, "{\"bypass\":%d}", &b) == 1) {
				is_valid = true;
				rs485_ok = setAPBypass(b);
			}
		} else if (strstr(payload, "\"timer\":")) {
			int t;
			if (sscanf(payload, "{\"timer\":%d}", &t) == 1) {
				is_valid = true;
				rs485_ok = setAPTimer(t);
			}
		}
		// --- [CASE 3: 상태 조회 (get_ap)] ---
		else if (strstr(payload, "\"get_ap\":\"state\"") != NULL
				|| strcmp(topic, topics.cmnd_ap_getall) == 0) {
			is_valid = true;
			is_query = true;
		}

		if (is_valid) {
			if (is_query) {
				rs485_ok = requestAPUpdate();
			} else {
				requestAPUpdate();
			}
			int result =
					is_query ?
							((rs485_ok == 0 && dev_status.ap.is_connected) ?
									1 : 0) :
							(rs485_ok == 0 ? 1 : 0);

			snprintf(response_ap, sizeof(response_ap),
					"{\"pwr\":%d,\"mode\":\"%s\",\"spd\":%d,\"uv\":%d,\"co2\":%d,\"pm25\":%d,\"pm10\":%d,\"pm1_0\":%d,\"temp\":%.1f,\"temp_out\":%.1f,\"humi\":%d,\"tvoc\":%d,\"filter\":%d,\"conn\":%d,\"result\":%d}",
					(strcmp(dev_status.ap.pwr, "ON") == 0 ? 1 : 0),
					dev_status.ap.mode, dev_status.ap.fan_speed,
					dev_status.ap.uv_on ? 1 : 0, dev_status.ap.co2,
					dev_status.ap.dust_pm25, dev_status.ap.dust_pm10,
					dev_status.ap.dust_pm1_0, dev_status.ap.temp_in,
					dev_status.ap.temp_out, dev_status.ap.humi,
					dev_status.ap.tvoc, dev_status.ap.filter_life,
					dev_status.ap.is_connected ? 1 : 0, result);

			if (MQTTPublish(&c, topics.stat_ap, &(MQTTMessage ) { QOS0, 0, 0, 0,
							response_ap, (int) strlen(response_ap) })
					== SUCCESS) {
				last_ap_pub_tick = HAL_GetTick();
				pub_done_ap = true;
				last_sent_ap = dev_status.ap;
			}
		} else {
			snprintf(response_ap, sizeof(response_ap),
					"{\"ap\":0,\"conn\":%d,\"result\":0}",
					dev_status.ap.is_connected ? 1 : 0);
			MQTTPublish(&c, topics.stat_ap, &(MQTTMessage ) { QOS0, 0, 0, 0,
							response_ap, (int) strlen(response_ap) });
		}
		return;
	}
	/* ======================================================================
	 * 5. [AC] LG 에어컨 (구조체 변수 기반 & 개별/통합 제어 최종 반영)
	 * ====================================================================== */
	else if (strstr(topic, "cmnd") != NULL && strstr(topic, "ac") != NULL) {
		bool is_valid = false;
		bool is_query = false;
		int modbus_ok = -1;
		char response_ac[512]; // PDF Page 10 로컬 고정 버퍼 독립 분리

		// [1. 연결 상태 체크] conn:0 이면 1회 Modbus 탐색 후 재판정
		if (!dev_status.ac.is_connected) {
			getACStatus();
			if (!dev_status.ac.is_connected) {
				snprintf(response_ac, sizeof(response_ac),
						"{\"ac\":0,\"conn\":0,\"result\":0}");
				MQTTPublish(&c, topics.stat_ac, &(MQTTMessage ) { QOS0, 0, 0, 0,
								response_ac, (int) strlen(response_ac) });
				return;
			}
		}

		// --- [CASE A: 통합 제어 (set_ac)] ---
		if (strcmp(topic, topics.cmnd_ac_setall)
				== 0|| strstr(payload, "\"set_ac\":") != NULL) {
			int p = -1;
			int tmp = -1;
			int spd = -1;
			char mod[16] = ""; // PDF Page 11 원본 크기 복원 및 신택스 에러 완벽 클리어
			if (sscanf(payload,
					"{\"set_ac\":{\"pwr\":%d,\"mode\":\"%15[^\"]\",\"temp\":%d,\"spd\":%d}}",
					&p, mod, &tmp, &spd) == 4) {
				uint8_t m_code = (strcmp(mod, "COOL") == 0) ? 0 :
									(strcmp(mod, "DRY") == 0) ? 1 :
									(strcmp(mod, "FAN") == 0) ? 2 :
									(strcmp(mod, "AUTO") == 0) ? 3 : 4;
				is_valid = true;
				modbus_ok = setACPower(p == 1);
				if (modbus_ok == 0)
					modbus_ok = setACMode(m_code);
				if (modbus_ok == 0)
					modbus_ok = setACTemperature((uint16_t) (tmp * 10));
				if (modbus_ok == 0)
					modbus_ok = setACWindSpeed((uint8_t) spd);
			}
		}
		// --- [CASE B: 개별 제어 4종 문법 가드 해제] ---
		else if (strstr(payload, "\"pwr\":") != NULL) {
			int p;
			if (sscanf(payload, "{\"pwr\":%d}", &p) == 1) {
				is_valid = true;
				modbus_ok = setACPower(p == 1);
			}
		} else if (strstr(payload, "\"mode\":") != NULL) {
			char mod[16] = ""; // PDF Page 11 완전 복원
			if (sscanf(payload, "{\"mode\":\"%15[^\"]\"}", mod) == 1) {
				uint8_t m_code = (strcmp(mod, "COOL") == 0) ? 0 :
									(strcmp(mod, "DRY") == 0) ? 1 :
									(strcmp(mod, "FAN") == 0) ? 2 :
									(strcmp(mod, "AUTO") == 0) ? 3 : 4;
				is_valid = true;
				modbus_ok = setACMode(m_code);
			}
		} else if (strstr(payload, "\"temp\":") != NULL) {
			int tmp;
			if (sscanf(payload, "{\"temp\":%d}", &tmp) == 1) {
				is_valid = true;
				modbus_ok = setACTemperature((uint16_t) (tmp * 10));
			}
		} else if (strstr(payload, "\"spd\":") != NULL) {
			int spd;
			if (sscanf(payload, "{\"spd\":%d}", &spd) == 1) {
				is_valid = true;
				modbus_ok = setACWindSpeed((uint8_t) spd);
			}
		} else if (strstr(payload, "\"get_ac\":\"state\"") != NULL
				|| strcmp(topic, topics.cmnd_ac_getall) == 0) {
			is_valid = true;
			is_query = true;
		}

		if (is_valid) {
			getACStatus();
			int result =
					is_query ?
							(dev_status.ac.is_connected ? 1 : 0) :
							(modbus_ok == 0 ? 1 : 0);
			snprintf(response_ac, sizeof(response_ac),
					"{\"pwr\":%d,\"mode\":\"%s\",\"temp\":%.1f,\"curr\":%.1f,\"spd\":%d,\"err\":%d,\"conn\":%d,\"result\":%d}",
					(strcmp(dev_status.ac.pwr, "ON") == 0 ? 1 : 0),
					dev_status.ac.mode, (double) dev_status.ac.temp_sel,
					(double) dev_status.ac.temp_curr, dev_status.ac.fan_speed,
					dev_status.ac.error_code,
					dev_status.ac.is_connected ? 1 : 0, result);
			if (MQTTPublish(&c, topics.stat_ac, &(MQTTMessage ) { QOS0, 0, 0, 0,
							response_ac, (int) strlen(response_ac) })
					== SUCCESS) {
				last_ac_pub_tick = HAL_GetTick();
				pub_done_ac = true;
			}
		} else {
			snprintf(response_ac, sizeof(response_ac),
					"{\"ac\":0,\"conn\":%d,\"result\":0}",
					dev_status.ac.is_connected ? 1 : 0);
			MQTTPublish(&c, topics.stat_ac, &(MQTTMessage ) { QOS0, 0, 0, 0,
							response_ac, (int) strlen(response_ac) });
		}
		return;
	}
	/* ======================================================================
	 * 6. [SYS] 시스템 제어 (이미지 규격: ALL_DATA, DEV_RESET 최종 반영)
	 * ====================================================================== */
	else if (strstr(topic, topics.cmnd_dev)) {
		char *p_buf = (char*) malloc(1536); // PDF Page 13 충분한 힙 스페이스 확보
		if (p_buf == NULL) {
			return;
		}

		// [CASE 1] 장치 물리 리셋 시퀀스
		if (strstr(payload, "\"reset\":\"DEV_RESET\"") != NULL) {
			snprintf(p_buf, 1536,
					"{\"device\":\"DEV_RESET\",\"conn\":0,\"result\":1}");
			MQTTPublish(&c, topics.stat_dev, &(MQTTMessage ) { QOS0, 0, 0, 0,
							p_buf, (int) strlen(p_buf) });
			osDelay(500);
			free(p_buf);
			NetConfig_ExecuteFlashEraseAndReboot(); // 최신 수립 물리 리셋 드라이버 가동
			return;
		}
		// [CASE 2] 모든 텔레메트리 일괄 가동 데이터 빌드 (ALL_DATA 포맷 완전 일치)
		else if (strstr(payload, "\"data\":\"ALL_DATA\"") != NULL) {
			if (osMutexAcquire(mqtt_mutex_id, 1000) == osOK) {
				uint32_t now = HAL_GetTick();

				snprintf(p_buf, 1536,
						"{\"device\":\"ALL_DATA\",\"conn\":1,\"result\":1}");
				MQTTPublish(&c, topics.stat_dev, &(MQTTMessage ) { QOS0, 0, 0,
								0, p_buf, (int) strlen(p_buf) });

				snprintf(p_buf, 1536,
						"{\"temp\":%.1f,\"humi\":%.1f,\"conn\":%d}",
						dev_status.th_in.temp, dev_status.th_in.humi,
						dev_status.th_in.is_connected ? 1 : 0);
				MQTTPublish(&c, topics.tele_th_in, &(MQTTMessage ) { QOS0, 0, 0,
								0, p_buf, (int) strlen(p_buf) });

				snprintf(p_buf, 1536,
						"{\"temp\":%.1f,\"humi\":%.1f,\"conn\":%d}",
						dev_status.th_out.temp, dev_status.th_out.humi,
						dev_status.th_out.is_connected ? 1 : 0);
				MQTTPublish(&c, topics.tele_th_out, &(MQTTMessage ) { QOS0, 0,
								0, 0, p_buf, (int) strlen(p_buf) });

				snprintf(p_buf, 1536,
						"{\"pm1_0\":%d,\"pm2_5\":%d,\"pm10\":%d,\"conn\":%d}",
						dev_status.dust.pm1_0, dev_status.dust.pm2_5,
						dev_status.dust.pm10,
						dev_status.dust.is_connected ? 1 : 0);
				MQTTPublish(&c, topics.tele_dust, &(MQTTMessage ) { QOS0, 0, 0,
								0, p_buf, (int) strlen(p_buf) });

				char r_list[128] = { 0 };
				int r_off = 0;
				for (int i = 0; i < 15; i++) {
					r_off += snprintf(r_list + r_off, sizeof(r_list) - r_off,
							"%d%s", dev_status.relay.ch[i] ? 1 : 0,
							(i < 14) ? "," : "");
				}
				snprintf(p_buf, 1536, "{\"relays\":[%s],\"conn\":1}", r_list);
				MQTTPublish(&c, topics.tele_ad, &(MQTTMessage ) { QOS0, 0, 0, 0,
								p_buf, (int) strlen(p_buf) });

				snprintf(p_buf, 1536,
						"{\"mode\":\"%s\",\"duty\":%d,\"conn\":%d}",
						dev_status.fan.mode, dev_status.fan.duty_percent,
						dev_status.fan.is_connected ? 1 : 0);
				MQTTPublish(&c, topics.tele_fan, &(MQTTMessage ) { QOS0, 0, 0,
								0, p_buf, (int) strlen(p_buf) });

				snprintf(p_buf, 1536,
						"{\"pwr\":%d,\"mode\":\"%s\",\"temp\":%.1f,\"curr\":%.1f,\"spd\":%d,\"err\":%d,\"conn\":%d}",
						(strcmp(dev_status.ac.pwr, "ON") == 0 ? 1 : 0),
						dev_status.ac.mode, (double) dev_status.ac.temp_sel,
						(double) dev_status.ac.temp_curr,
						dev_status.ac.fan_speed, dev_status.ac.error_code,
						dev_status.ac.is_connected ? 1 : 0);
				MQTTPublish(&c, topics.tele_ac, &(MQTTMessage ) { QOS0, 0, 0, 0,
								p_buf, (int) strlen(p_buf) });

				uint8_t pb_id = (uint8_t) dev_status.pwr_ch[0].b_id;
				int pb_len = snprintf(p_buf, 1536, "{\"b_id\":%d,\"pb\":[",
						pb_id);
				for (int i = 0; i < 8; i++) {
					int sw_stat =
							(strcmp(dev_status.pwr_ch[i].pwr, "ON") == 0) ?
									1 : 0;
					pb_len +=
							snprintf(p_buf + pb_len, 1536 - pb_len,
									"{\"ch\":%d,\"sw\":%d,\"c\":%.2f,\"w\":%.1f,\"v\":%d}%s",
									(i + 1), sw_stat, dev_status.pwr_ch[i].curr,
									dev_status.pwr_ch[i].watt,
									(int) dev_status.pwr_ch[i].volt,
									(i < 7) ? "," : "");
				}
				snprintf(p_buf + pb_len, 1536 - pb_len, "],\"conn\":1}");
				MQTTPublish(&c, topics.tele_pb, &(MQTTMessage ) { QOS0, 0, 0, 0,
								p_buf, (int) strlen(p_buf) });

				snprintf(p_buf, 1024,
						"{\"pwr\":%d,\"mode\":\"%s\",\"spd\":%d,\"uv\":%d,\"co2\":%d,\"pm25\":%d,\"pm10\":%d,\"pm1_0\":%d,\"temp\":%.1f,\"temp_out\":%.1f,\"humi\":%d,\"tvoc\":%d,\"filter\":%d,\"conn\":%d}",
						!strcmp(dev_status.ap.pwr, "ON"), dev_status.ap.mode,
						dev_status.ap.fan_speed, dev_status.ap.uv_on,
						dev_status.ap.co2, dev_status.ap.dust_pm25,
						dev_status.ap.dust_pm10, dev_status.ap.dust_pm1_0,
						dev_status.ap.temp_in, dev_status.ap.temp_out,
						dev_status.ap.humi, dev_status.ap.tvoc,
						dev_status.ap.filter_life,
						dev_status.ap.is_connected ? 1 : 0);
				MQTTPublish(&c, topics.tele_ap, &(MQTTMessage ) { QOS0, 0, 0, 0,
								p_buf, (int) strlen(p_buf) });

				snprintf(p_buf, 1536,
						"{\"in\":[%d,%d,%d,%d],\"count\":[%lu,%lu,%lu,%lu],\"conn\":1}",
						dev_status.in_stat.p4, dev_status.in_stat.p5,
						dev_status.in_stat.p6, dev_status.in_stat.p7,
						(unsigned long) dev_status.in_stat.cnt4,
						(unsigned long) dev_status.in_stat.cnt5,
						(unsigned long) dev_status.in_stat.cnt6,
						(unsigned long) dev_status.in_stat.cnt7);
				MQTTPublish(&c, topics.tele_input, &(MQTTMessage ) { QOS0, 0, 0,
								0, p_buf, (int) strlen(p_buf) });

				last_th_in_pub_tick = now;
				last_th_out_pub_tick = now;
				last_dust_pub_tick = now;
				last_ac_pub_tick = now;
				last_fan_pub_tick = now;
				last_pwr_all_pub_tick = now;
				last_ap_pub_tick = now;
				last_input_pub_tick = now;
				pub_done_pwr_all = true;
				pub_done_input = true;
				osMutexRelease(mqtt_mutex_id);
			}
			free(p_buf);
			return;
		}
        // [CASE 3] 런타임 셋넷 고정 IP 처리 스페이스 (PDF Page 16~17 완전 싱크화)
        else if (strstr(payload, "\"set_net\"") != NULL) {
            NetRuntimeConfig new_cfg = g_net_cfg;
            int ip[4] = {0};
            int sn[4] = {0};
            int gw[4] = {0};
            int dns[4] = {0}; // 포인터 파싱 버그용 배열 스페이스 규격 확실히 유지
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
                MQTTPublish(&c, topics.stat_dev, &(MQTTMessage){QOS0, 0, 0, 0, p_buf, (int)strlen(p_buf)});
                osDelay(200);
                free(p_buf);
                NetConfig_SaveAndApply(&new_cfg);
                return;
            } else {
                snprintf(p_buf, 1536, "{\"device\":\"SET_NET\",\"conn\":1,\"result\":0}");
                MQTTPublish(&c, topics.stat_dev, &(MQTTMessage){QOS0, 0, 0, 0, p_buf, (int)strlen(p_buf)});
                free(p_buf);
                return;
            }
        }
        // [CASE 4] 브로커 목적지 주소 각인 스페이스
        else if (strstr(payload, "\"set_broker\"") != NULL) {
            NetRuntimeConfig new_cfg = g_net_cfg;
            int ip[4] = {0};
            int port = 0;
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
                MQTTPublish(&c, topics.stat_dev, &(MQTTMessage){QOS0, 0, 0, 0, p_buf, (int)strlen(p_buf)});
                osDelay(200);
                free(p_buf);
                NetConfig_SaveAndApply(&new_cfg);
                return;
            } else {
                snprintf(p_buf, 1536, "{\"device\":\"SET_BROKER\",\"conn\":1,\"result\":0}");
                MQTTPublish(&c, topics.stat_dev, &(MQTTMessage){QOS0, 0, 0, 0, p_buf, (int)strlen(p_buf)});
                free(p_buf);
                return;
            }
        }
        // [CASE 5] 원격 강제 DHCP 원상 복구 스페이스
        else if (strstr(payload, "\"set_net_reset\"") != NULL) {
            NetRuntimeConfig new_cfg = g_net_cfg;
            new_cfg.net_mode = 1;
            snprintf(p_buf, 1536, "{\"device\":\"SET_NET_RESET\",\"conn\":1,\"result\":1}");
            MQTTPublish(&c, topics.stat_dev, &(MQTTMessage){QOS0, 0, 0, 0, p_buf, (int)strlen(p_buf)});
            osDelay(200);
            free(p_buf);
            NetConfig_SaveAndApply(&new_cfg);
            return;
        }
        free(p_buf);
        return;
    }
    /* ======================================================================
     * 7. [INPUT] 외부 입력 4채널 조회/카운트 초기화 (v2.0 신규) [★깨졌던 따옴표 전면 교정 완료]
     * ====================================================================== */
    else if (strstr(topic, topics.cmnd_input) != NULL) {
        char resp[256]; // 256바이트 고정 배열로 정확히 선언 복원
        if (strstr(payload, "\"get_input\"") != NULL) {
            if (osMutexAcquire(mqtt_mutex_id, 200) == osOK) {
                snprintf(resp, sizeof(resp), "{\"in\":[%d,%d,%d,%d],\"count\":[%lu,%lu,%lu,%lu],\"conn\":1,\"result\":1}", dev_status.in_stat.p4, dev_status.in_stat.p5, dev_status.in_stat.p6, dev_status.in_stat.p7, (unsigned long)dev_status.in_stat.cnt4, (unsigned long)dev_status.in_stat.cnt5, (unsigned long)dev_status.in_stat.cnt6, (unsigned long)dev_status.in_stat.cnt7);
                MQTTPublish(&c, topics.stat_input, &(MQTTMessage){QOS0, 0, 0, 0, resp, (int)strlen(resp)});
                osMutexRelease(mqtt_mutex_id);
            }
        } else if (strstr(payload, "\"reset_count\"") != NULL) {
            if (osMutexAcquire(mqtt_mutex_id, 200) == osOK) {
                dev_status.in_stat.cnt4 = dev_status.in_stat.cnt5 = dev_status.in_stat.cnt6 = dev_status.in_stat.cnt7 = 0;
                snprintf(resp, sizeof(resp), "{\"in\":[%d,%d,%d,%d],\"count\":[0,0,0,0],\"conn\":1,\"result\":1}", dev_status.in_stat.p4, dev_status.in_stat.p5, dev_status.in_stat.p6, dev_status.in_stat.p7);
                MQTTPublish(&c, topics.stat_input, &(MQTTMessage){QOS0, 0, 0, 0, resp, (int)strlen(resp)});
                osMutexRelease(mqtt_mutex_id);
            }
        }
        return;
    }
}
