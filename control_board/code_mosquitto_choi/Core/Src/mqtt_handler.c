#include "mqtt_handler.h"
#include "app.h"
#include "power_board.h"
#include "himpel.h"
#include "app_loop.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

extern void getAPStatusAll(APData* dest);

void MQTT_MessageArrived(MessageData* md) {
	MQTTMessage* message = md->message;
	char topic[TOPIC_SIZE], payload[256], response[640]; // 응답 버퍼 확장 (8채널 대비)

	snprintf(topic, sizeof(topic), "%.*s", md->topicName->lenstring.len, md->topicName->lenstring.data);
	snprintf(payload, sizeof(payload), "%.*s", (int)message->payloadlen, (char*)message->payload);

	printf("\r\n[MQTT RX] Topic: %s,%s\r\n", topic,payload);

	/* ======================================================================
           1. [AD] 릴레이 제어 (구조체 변수 기반 & 이미지 규격 반영)
           ====================================================================== */
	if (strstr(topic, "cmnd") != NULL && strstr(topic, "ad") != NULL) {
		//토픽 문자열에 ad와 cmnd가 들어있어야 진입
		bool is_valid = false;

		// 1. [전체 셋팅 명령어] {"set_ad":"100000000000000"}
		if (strstr(payload, "\"set_ad\"")) {
			char ad_str[32] = {0};
			// JSON 포맷에 맞게 유연하게 문자열 파싱
			if (sscanf(payload, "{\"set_ad\":\"%15[0-1]\"", ad_str) == 1 && strlen(ad_str) == 15) {
				is_valid = true;

				//printf("[RX] Executing 15-Ch Sequential Control (Interval 200ms)...\r\n");
				for (int i = 0; i < 15; i++) {
					bool target_state = (ad_str[i] == '1');

					// 현재 하드웨어의 실제 핀 상태 확인
					bool current_state = RLY_GetStatus(i + 1);

					// ★ 핵심 가드: 상태 변경이 필요할 때만 실제 물리 제어 및 시차 부여
					if (current_state != target_state) {
						RLY_SetStatus(i + 1, target_state);
						osDelay(500); // 순차 작동하여 보드 전원 보호
					}
				}
				printf("[RX] Sequential Control Complete.\r\n");
			}
		}
		// 2. [개별 셋팅 명령어] {"ch":1, "relay_1":1}
		else if (strstr(payload, "\"ch\"")) {
			int ch = -1, val = -1;
			// 엑셀 규격 기조에 맞추어 공백 및 문자를 안전하게 긁어오는 파싱 가드 적용
			char *ch_ptr = strstr(payload, "\"ch\"");
			char *rly_ptr = strstr(payload, "\"relay_");

			if (ch_ptr && rly_ptr) {
				// "ch":1 추출
				sscanf(ch_ptr, "\"ch\"%*[^0-9]%d", &ch);
				// "relay_1":1 추출
				sscanf(rly_ptr, "\"relay_%*d\"%*[^0-9]%d", &val);

				if (ch >= 1 && ch <= 15 && (val == 0 || val == 1)) {
					bool target_state = (val == 1);
					bool current_state = RLY_GetStatus(ch);

					// 개별 제어도 중복 제어가 아니며 상태가 바뀔 때만 물리 핀 트리거 실행
					if (current_state != target_state) {
						RLY_SetStatus(ch, target_state);
					}
					is_valid = true;
				}
			}
		}
		// 3. [전체 상태 가져오기 명령어] {"get_ad":"state"}
		else if (strstr(payload, "\"get_ad\"")) {
			is_valid = true;
			printf("[RX] Request Status: get_ad received.\r\n");
		}

		/* ── [Phase 3] 응답 프로토콜 조립 및 전송 ── */
		char r_list[128] = {0};
		int r_off = 0;
		for (int i = 0; i < 15; i++) {
			r_off += snprintf(r_list + r_off, sizeof(r_list) - r_off, "%d%s",
					dev_status.relay.ch[i] ? 1 : 0, (i < 14) ? "," : "");
		}

		snprintf(response, sizeof(response),
				"{\"relays\":[%s],\"conn\":1,\"result\":%d}",
				r_list, is_valid ? 1 : 0);

		if (MQTTPublish(&c, topics.stat_ad, &(MQTTMessage){QOS0, 0, 0, 0, response, (int)strlen(response)}) == SUCCESS) {
			// [추가] 제어 응답이 성공적으로 나갔으므로, 센서 태스크의 주기 보고를 리셋함
			pub_done_relay = true;               // 보고 완료 상태로 변경
			last_state_pub_tick = HAL_GetTick(); // 다음 보고 시점을 현재부터 10분 뒤로 미룸
		}
		return;
	}

	/* ======================================================================
               2. [FAN] 쿨러 팬 (구조체 변수 기반 & 이미지 규격 반영)
               ====================================================================== */
	else if (strstr(topic, "cmnd") != NULL && strstr(topic, "fan") != NULL) {
		bool is_valid = false;

		// 1. [제어 명령 처리] 토픽: "dev/cmnd/fan"
		// 테스트 케이스: {"set_fan":{"mode":"AUTO","duty":0}} 또는 {"set_fan":{"mode":"MANUAL","duty":100}}
		if (strstr(payload, "\"set_fan\"") != NULL) {
			char mod[12] = {0};
			int fan_duty = -1;

			// 중첩 JSON 파싱 (sscanf의 한계를 고려해 유연하게 설계)
			if (sscanf(payload, "{\"set_fan\":{\"mode\":\"%11[^\"]\",\"duty\":%d}}", mod, &fan_duty) >= 2) {
				if (fan_duty >= 0 && fan_duty <= 100) {
					strncpy(dev_status.fan.mode, mod, sizeof(dev_status.fan.mode) - 1);
					dev_status.fan.duty_percent = fan_duty;

					// MANUAL 모드일 때만 PWM 즉시 반영
					if (strcmp(mod, "MANUAL") == 0) {
						__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, fan_duty * 10);
					}
					// AUTO 모드는 메인 루프나 센서 태스크에서 온도에 따라 duty 결정
					is_valid = true;
				}
			}
		}
		// 2. [상태 요청 처리] 토픽: "dev/cmnd/fan/get" 또는 페이로드에 "GET" 포함
		// 테스트 케이스: mosquitto_pub ... -t "dev/cmnd/fan/get" -m "{\"info\":\"GET\"}"
		else if (strstr(topic, "/get") != NULL || strstr(payload, "\"info\":\"GET\"") != NULL
				|| strstr(payload, "\"get_fan\":\"state\"") != NULL) {
			is_valid = true;
		}

		/* --- [응답 생성: 이미지 규격 반영] --- */
		// 규격서 포맷: {"mode":"MANUAL","duty":50,"conn":1,"result":1}
		if (dev_status.fan.is_connected) {
			snprintf(response, sizeof(response),
					"{\"mode\":\"%s\",\"duty\":%d,\"conn\":1,\"result\":%d}",
					dev_status.fan.mode, dev_status.fan.duty_percent, is_valid ? 1 : 0);
		} else {
			// 미연결 시 규격: {"fan":0,"conn":0,"result":1}
			snprintf(response, sizeof(response), "{\"fan\":0,\"conn\":0,\"result\":1}");
		}

		// 응답 토픽: dev/stat/fan
		if (MQTTPublish(&c, topics.stat_fan, &(MQTTMessage){QOS0, 0, 0, 0, response, (int)strlen(response)}) == SUCCESS) {

			// [추가된 리셋 로직]
			// 제어 응답이 나갔으므로, 센서 태스크의 10분 주기 보고 타이머를 현재 시점으로 리셋합니다.
			pub_done_fan = true;               // 보고 완료 플래그 세팅
			last_fan_pub_tick = HAL_GetTick(); // 다음 보고 시점을 현재부터 10분 뒤로 미룸

			// 마지막 전송 데이터 백업 (센서 태스크에서 변화 감지 시 참조용)
			last_sent_fan_duty = dev_status.fan.duty_percent;
			strncpy(last_sent_fan_mode, dev_status.fan.mode, sizeof(last_sent_fan_mode)-1);
		}
		return;
	}


	/* ======================================================================
       3. [PB] 전력보드 제어 (구조체 변수 기반 & 설계서 규격 완벽 반영)
       ====================================================================== */
	// "pb"가 포함된 명령 토픽인지 확인
	/* MQTT 메시지 수신 시 호출되는 섹션 내 PowerBoard 처리 부분 */
	else if (strstr(topic, "cmnd") != NULL && strstr(topic, "pb") != NULL) {
		// 1. 연결 상태 체크
		uint8_t current_pb_id = (uint8_t)dev_status.pwr_ch[0].b_id;
		if (!dev_status.is_pwr_connected) {
			snprintf(response, sizeof(response),"{\"b_id\":%d,\"pb\":0,\"conn\":1,\"result\":0}", current_pb_id);
			MQTTPublish(&c, topics.stat_pb, &(MQTTMessage){QOS0, 0, 0, 0, response, (int)strlen(response)});
			return;
		}

		bool is_valid = false;
		int  tid = 0, tch = -1, sw = -1;
		char pwr_str[24] = {0};

		// A. [개별 채널 제어] 파싱 및 가드
		if (sscanf(payload, "{\"b_id\":%d,\"ch\":%d,\"sw\":%d}", &tid, &tch, &sw) == 3 &&
				tid == current_pb_id && tch >= 1 && tch <= 8 && (sw == 0 || sw == 1)) {

			PowerBoard_ControlSingle((uint8_t)tid, (uint8_t)tch, (uint8_t)sw);
			is_valid = true;
		}
		// B. [8ch 전체 채널 제어] 파싱 및 가드 (set_pb "11111111" 수용부)
		else if (strstr(payload, "set_pb")) {
			int res = sscanf(payload, "{\"b_id\":%d,\"set_pb\":\"%23[^\"]\"}", &tid, pwr_str);
			if (res != 2) {
				res = sscanf(payload, "{\"id\":%d,\"set_pb\":\"%23[^\"]\"}", &tid, pwr_str);
			}

			if (res == 2 && tid == current_pb_id && strlen(pwr_str) == 8) {
				uint8_t states[8];
				bool err = false;
				for (int i = 0; i < 8; i++) {
					if (pwr_str[i] != '0' && pwr_str[i] != '1') {
						err = true; break;
					}
					states[i] = (pwr_str[i] == '1');
				}
				if (!err) {
					PowerBoard_ControlAll((uint8_t)tid, states);
					is_valid = true;
				}
			}
		}
		// C. [전체 상태 요청 제어] 파싱 및 가드
		else if (strstr(payload, "get_pb")) {
			if (sscanf(payload, "{\"b_id\":%d,\"get_pb\":\"state\"}", &tid) == 1 ||
					sscanf(payload, "{\"id\":%d,\"get_pb\":\"state\"}", &tid) == 1) {
				if (tid == current_pb_id) is_valid = true;
			}
		}

		// --- [응답 생성: 전류 'c' 추가 및 전체 중괄호 보정] ---
		if (is_valid) {
			// 제어 완료 즉시 최신 485 레지스터 캐시 새로고침
			PowerBoard_UpdateAllData(dev_status.pwr_ch);

			int len = snprintf(response, sizeof(response), "{\"b_id\":%d,\"pb\":[", current_pb_id);
			for (int i = 0; i < 8; i++) {
				int s = (strcmp(dev_status.pwr_ch[i].pwr, "ON") == 0) ? 1 : 0;
				len += snprintf(response + len, sizeof(response) - len,
						"{\"ch\":%d,\"sw\":%d,\"c\":%.2f,\"w\":%.1f,\"v\":%d}%s",
						(i + 1), s,
						dev_status.pwr_ch[i].curr,
						dev_status.pwr_ch[i].watt,
						(int)dev_status.pwr_ch[i].volt,
						(i < 7) ? "," : "");
			}
			snprintf(response + len, sizeof(response) - len, "],\"conn\":1,\"result\":1}");
		}
		else {
			// 실패 시 응답 (result: 0)
			snprintf(response, sizeof(response),
					"{\"b_id\":%d,\"pb\":0,\"conn\":1,\"result\":0}", current_pb_id);
		}

		if (MQTTPublish(&c, topics.stat_pb, &(MQTTMessage){QOS0, 0, 0, 0, response, (int)strlen(response)}) == SUCCESS) {

			// [추가된 리셋 로직]
			// 제어 응답이 성공했으므로 전원 보드 관련 모든 보고 주기를 현재 시점으로 리셋합니다.
			uint32_t current_tick = HAL_GetTick();

			pub_done_pwr_all = true;              // 통합 보고 완료 표시
			last_pwr_all_pub_tick = current_tick; // 다음 10분 보고 시점을 현재부터 다시 계산

			// 개별 채널 플래그들도 리셋하여 중복 보고 방지
			for(int i = 0; i < 8; i++) {
				pub_done_pwr[i] = true;
				// 마지막 전송 데이터 업데이트 (변화 감지 로직용)
				last_sent_pwr[i].watt = dev_status.pwr_ch[i].watt;
			}
		}
	}
	/* ======================================================================
       4. [AP] Himpel 공기청정기 (개별/전체 제어 및 이미지 규격 반영)
       ====================================================================== */
	else if (strstr(topic, "cmnd") != NULL && strstr(topic, "ap") != NULL) {
		bool is_valid    = false;
		bool is_query    = false;
		int  rs485_ok    = -1;
		char response[512];

		// [1. 연결 상태 체크] conn:0 이면 1회 RS485 탐색 후 재판정
		if (!dev_status.ap.is_connected) {
			requestAPUpdate();
			if (!dev_status.ap.is_connected) {
				snprintf(response, sizeof(response), "{\"ap\":0,\"conn\":0,\"result\":0}");
				MQTTPublish(&c, topics.stat_ap, &(MQTTMessage){QOS0, 0, 0, 0, response, (int)strlen(response)});
				return;
			}
		}

		// --- [CASE 1: 전체 설정 (set_ap)] ---
		if (strcmp(topic, topics.cmnd_ap_setall) == 0 || strstr(payload, "\"set_ap\":") != NULL) {
			int p, s, u, fr, bp, tm; char m[16] = {0};
			if (sscanf(payload, "{\"set_ap\":{\"pwr\":%d,\"mode\":\"%15[^\"]\",\"spd\":%d,\"uv\":%d,\"filter_reset\":%d,\"bypass\":%d,\"timer\":%d}}",
					&p, m, &s, &u, &fr, &bp, &tm) == 7) {
				is_valid  = true;
				rs485_ok  = setAPAll(p, m, s, u, fr, bp, tm);
			}
		}
		// --- [CASE 2: 개별 제어 7종] ---
		else if (strstr(payload, "\"pwr\":")) {
			int p;
			if (sscanf(payload, "{\"pwr\":%d}", &p) == 1) {
				is_valid = true;
				rs485_ok = setAPPower(p);
			}
		}
		else if (strstr(payload, "\"mode\":")) {
			char m[16] = {0};
			if (sscanf(payload, "{\"mode\":\"%15[^\"]\"}", m) == 1) {
				is_valid = true;
				rs485_ok = setAPMode(m);
			}
		}
		else if (strstr(payload, "\"spd\":")) {
			int s;
			if (sscanf(payload, "{\"spd\":%d}", &s) == 1) {
				is_valid = true;
				rs485_ok = setAPSpeed(s);
			}
		}
		else if (strstr(payload, "\"uv\":")) {
			int u;
			if (sscanf(payload, "{\"uv\":%d}", &u) == 1) {
				is_valid = true;
				rs485_ok = setAPUV(u);
			}
		}
		else if (strstr(payload, "\"filter_reset\":")) {
			int f;
			if (sscanf(payload, "{\"filter_reset\":%d}", &f) == 1) {
				is_valid = true;
				rs485_ok = setAPFilterReset(f);
			}
		}
		else if (strstr(payload, "\"bypass\":")) {
			int b;
			if (sscanf(payload, "{\"bypass\":%d}", &b) == 1) {
				is_valid = true;
				rs485_ok = setAPBypass(b);
			}
		}
		else if (strstr(payload, "\"timer\":")) {
			int t;
			if (sscanf(payload, "{\"timer\":%d}", &t) == 1) {
				is_valid = true;
				rs485_ok = setAPTimer(t);
			}
		}
		/* --- [CASE 3: 상태 조회 (get_ap)] --- */
		else if (strstr(payload, "\"get_ap\":\"state\"") != NULL || strcmp(topic, topics.cmnd_ap_getall) == 0) {
			is_valid = true;
			is_query = true;
		}

		if (is_valid) {
			if (is_query) {
				rs485_ok = requestAPUpdate();
			} else {
				requestAPUpdate();
			}

			int result = is_query
					? ((rs485_ok == 0 && dev_status.ap.is_connected) ? 1 : 0)
					: (rs485_ok == 0 ? 1 : 0);

			snprintf(response, sizeof(response),
					"{\"pwr\":%d,\"mode\":\"%s\",\"spd\":%d,\"uv\":%d,\"co2\":%d,\"pm25\":%d,\"pm10\":%d,\"pm1_0\":%d,\"temp\":%.1f,\"temp_out\":%.1f,\"humi\":%d,\"tvoc\":%d,\"filter\":%d,\"conn\":%d,\"result\":%d}",
					(strcmp(dev_status.ap.pwr, "ON") == 0 ? 1 : 0),
					dev_status.ap.mode,
					dev_status.ap.fan_speed,
					dev_status.ap.uv_on ? 1 : 0,
					dev_status.ap.co2,
					dev_status.ap.dust_pm25,
					dev_status.ap.dust_pm10,
					dev_status.ap.dust_pm1_0,
					dev_status.ap.temp_in,
					dev_status.ap.temp_out,
					dev_status.ap.humi,
					dev_status.ap.tvoc,
					dev_status.ap.filter_life,
					dev_status.ap.is_connected ? 1 : 0,
					result);

			if (MQTTPublish(&c, topics.stat_ap, &(MQTTMessage){QOS0, 0, 0, 0, response, (int)strlen(response)}) == SUCCESS) {
				last_ap_pub_tick = HAL_GetTick();
				pub_done_ap = true;
				last_sent_ap = dev_status.ap;
			}
		} else {
			snprintf(response, sizeof(response), "{\"ap\":0,\"conn\":%d,\"result\":0}",
					dev_status.ap.is_connected ? 1 : 0);
			MQTTPublish(&c, topics.stat_ap, &(MQTTMessage){QOS0, 0, 0, 0, response, (int)strlen(response)});
		}
		return;
	}

	/* ======================================================================
       5. [AC] LG 에어컨 (구조체 변수 기반 & 개별/통합 제어 최종 반영)
       ====================================================================== */
	else if (strstr(topic, "cmnd") != NULL && strstr(topic, "ac") != NULL) {
		bool is_valid = false;
		bool is_query = false;
		int  modbus_ok = -1;
		char response[512];

		// [1. 연결 상태 체크] conn:0 이면 1회 Modbus 탐색 후 재판정
		if (!dev_status.ac.is_connected) {
			getACStatus();
			if (!dev_status.ac.is_connected) {
				snprintf(response, sizeof(response), "{\"ac\":0,\"conn\":0,\"result\":0}");
				MQTTPublish(&c, topics.stat_ac, &(MQTTMessage){QOS0, 0, 0, 0, response, (int)strlen(response)});
				return;
			}
		}

		// --- [CASE A: 통합 제어 (set_ac)] ---
		if (strcmp(topic, topics.cmnd_ac_setall) == 0 || strstr(payload, "\"set_ac\":") != NULL) {
			int p = -1, tmp = -1, spd = -1; char mod[16] = "";
			if (sscanf(payload, "{\"set_ac\":{\"pwr\":%d,\"mode\":\"%15[^\"]\",\"temp\":%d,\"spd\":%d}}",
					&p, mod, &tmp, &spd) == 4) {
				uint8_t m_code = (strcmp(mod, "COOL") == 0) ? 0 : (strcmp(mod, "DRY") == 0) ? 1 :
						(strcmp(mod, "FAN") == 0) ? 2 : (strcmp(mod, "AUTO") == 0) ? 3 : 4;
				is_valid = true;
				modbus_ok = setACPower(p == 1);
				if (modbus_ok == 0) modbus_ok = setACMode(m_code);
				if (modbus_ok == 0) modbus_ok = setACTemperature((uint16_t)(tmp * 10));
				if (modbus_ok == 0) modbus_ok = setACWindSpeed((uint8_t)spd);
			}
		}
		// --- [CASE B: 개별 제어 4종] ---
		else if (strstr(payload, "\"pwr\":") != NULL) {
			int p;
			if (sscanf(payload, "{\"pwr\":%d}", &p) == 1) {
				is_valid = true;
				modbus_ok = setACPower(p == 1);
			}
		}
		else if (strstr(payload, "\"mode\":") != NULL) {
			char mod[16] = "";
			if (sscanf(payload, "{\"mode\":\"%15[^\"]\"}", mod) == 1) {
				uint8_t m_code = (strcmp(mod, "COOL") == 0) ? 0 : (strcmp(mod, "DRY") == 0) ? 1 :
						(strcmp(mod, "FAN") == 0) ? 2 : (strcmp(mod, "AUTO") == 0) ? 3 : 4;
				is_valid = true;
				modbus_ok = setACMode(m_code);
			}
		}
		else if (strstr(payload, "\"temp\":") != NULL) {
			int tmp;
			if (sscanf(payload, "{\"temp\":%d}", &tmp) == 1) {
				is_valid = true;
				modbus_ok = setACTemperature((uint16_t)(tmp * 10));
			}
		}
		else if (strstr(payload, "\"spd\":") != NULL) {
			int spd;
			if (sscanf(payload, "{\"spd\":%d}", &spd) == 1) {
				is_valid = true;
				modbus_ok = setACWindSpeed((uint8_t)spd);
			}
		}
		else if (strstr(payload, "\"get_ac\":\"state\"") != NULL || strcmp(topic, topics.cmnd_ac_getall) == 0) {
			is_valid = true;
			is_query = true;
		}

		if (is_valid) {
			getACStatus();

			int result = is_query
					? (dev_status.ac.is_connected ? 1 : 0)
					: (modbus_ok == 0 ? 1 : 0);

			snprintf(response, sizeof(response),
					"{\"pwr\":%d,\"mode\":\"%s\",\"temp\":%.1f,\"curr\":%.1f,\"spd\":%d,\"err\":%d,\"conn\":%d,\"result\":%d}",
					(strcmp(dev_status.ac.pwr, "ON") == 0 ? 1 : 0),
					dev_status.ac.mode,
					(double)dev_status.ac.temp_sel,
					(double)dev_status.ac.temp_curr,
					dev_status.ac.fan_speed,
					dev_status.ac.error_code,
					dev_status.ac.is_connected ? 1 : 0,
					result);

			if (MQTTPublish(&c, topics.stat_ac, &(MQTTMessage){QOS0, 0, 0, 0, response, (int)strlen(response)}) == SUCCESS) {
				last_ac_pub_tick = HAL_GetTick();
				pub_done_ac = true;
			}
		} else {
			snprintf(response, sizeof(response), "{\"ac\":0,\"conn\":%d,\"result\":0}",
					dev_status.ac.is_connected ? 1 : 0);
			MQTTPublish(&c, topics.stat_ac, &(MQTTMessage){QOS0, 0, 0, 0, response, (int)strlen(response)});
		}
		return;
	}
	/* ======================================================================
           5. [SYS] 시스템 제어 (이미지 규격: ALL_DATA, DEV_RESET 최종 반영)
           ====================================================================== */
	else if (strstr(topic, topics.cmnd_dev)) {
		// 1536바이트의 충분한 버퍼 확보 (ALL_DATA 응답용)
		char *p_buf = (char *)malloc(1536);
		if (p_buf == NULL) return;

		/* --- [CASE 1: 장치 리셋] 규격: {"reset":"DEV_RESET"} --- */
		if (strstr(payload, "\"reset\":\"DEV_RESET\"") != NULL) {
			// 성공 응답 규격: {"device":"DEV_RESET","conn":0,"result":1}
			snprintf(p_buf, 1536, "{\"device\":\"DEV_RESET\",\"conn\":0,\"result\":1}");
			MQTTPublish(&c, topics.stat_dev, &(MQTTMessage){QOS0, 0, 0, 0, p_buf, (int)strlen(p_buf)});

			//printf("[SYS] Reset Command Success. Rebooting in 500ms...\r\n");
			osDelay(500);           // MQTT 전송 완료 시간 확보
			HAL_NVIC_SystemReset(); // 하드웨어 리셋 실행
		}

		/* --- [CASE 2: 모든 데이터 가져오기] 규격: {"data":"ALL_DATA"} --- */
		else if (strstr(payload, "\"data\":\"ALL_DATA\"") != NULL) {
			if (osMutexAcquire(mqtt_mutex_id, 1000) == osOK) {
				uint32_t now = HAL_GetTick();

				// 1. [ACK 응답]
				snprintf(p_buf, 1536, "{\"device\":\"ALL_DATA\",\"conn\":1,\"result\":1}");
				MQTTPublish(&c, topics.stat_dev, &(MQTTMessage){QOS0, 0, 0, 0, p_buf, (int)strlen(p_buf)});

				// 2. 내부 온습도
				snprintf(p_buf, 1536, "{\"temp\":%.1f,\"humi\":%.1f,\"conn\":%d}",
						dev_status.th_in.temp, dev_status.th_in.humi, dev_status.th_in.is_connected ? 1 : 0);
				MQTTPublish(&c, topics.tele_th_in, &(MQTTMessage){QOS0, 0, 0, 0, p_buf, (int)strlen(p_buf)});

				// 3. 외부 온습도
				snprintf(p_buf, 1536, "{\"temp\":%.1f,\"humi\":%.1f,\"conn\":%d}",
						dev_status.th_out.temp, dev_status.th_out.humi, dev_status.th_out.is_connected ? 1 : 0);
				MQTTPublish(&c, topics.tele_th_out, &(MQTTMessage){QOS0, 0, 0, 0, p_buf, (int)strlen(p_buf)});

				// 4. 미세먼지
				snprintf(p_buf, 1536, "{\"pm1_0\":%d,\"pm2_5\":%d,\"pm10\":%d,\"conn\":%d}",
						dev_status.dust.pm1_0, dev_status.dust.pm2_5, dev_status.dust.pm10, dev_status.dust.is_connected ? 1 : 0);
				MQTTPublish(&c, topics.tele_dust, &(MQTTMessage){QOS0, 0, 0, 0, p_buf, (int)strlen(p_buf)});

				// 5. 릴레이 (AD)
				char r_list[128] = {0}; int r_off = 0;
				for(int i=0; i<15; i++) {
					r_off += snprintf(r_list + r_off, sizeof(r_list) - r_off, "%d%s",
							dev_status.relay.ch[i] ? 1 : 0, (i < 14) ? "," : "");
				}
				snprintf(p_buf, 1536, "{\"relays\":[%s],\"conn\":1}", r_list);
				MQTTPublish(&c, topics.tele_ad, &(MQTTMessage){QOS0, 0, 0, 0, p_buf, (int)strlen(p_buf)});

				// 6. 내부 팬 (Fan)
				snprintf(p_buf, 1536, "{\"mode\":\"%s\",\"duty\":%d,\"conn\":%d}",
						dev_status.fan.mode, dev_status.fan.duty_percent, dev_status.fan.is_connected ? 1 : 0);
				MQTTPublish(&c, topics.tele_fan, &(MQTTMessage){QOS0, 0, 0, 0, p_buf, (int)strlen(p_buf)});

				// 7. 에어컨 (AC)
				snprintf(p_buf, 1536,
						"{\"pwr\":%d,\"mode\":\"%s\",\"temp\":%.1f,\"curr\":%.1f,\"spd\":%d,\"err\":%d,\"conn\":%d}",
						(strcmp(dev_status.ac.pwr, "ON") == 0 ? 1 : 0), // pwr: 문자열 "ON"이면 1, 아니면 0 (숫자형)
						dev_status.ac.mode,
						(double)dev_status.ac.temp_sel,   // 희망 온도 (예: 25.0)
						(double)dev_status.ac.temp_curr,  // 현재 온도 (예: 24.5)
						dev_status.ac.fan_speed,         // 바람 세기 (1, 2, 3, 4, 7)
						dev_status.ac.error_code,        // 에러 코드 (0:정상, 1~999:에러)
						dev_status.ac.is_connected ? 1 : 0
				);
				MQTTPublish(&c, topics.tele_ac, &(MQTTMessage){QOS0, 0, 0, 0, p_buf, (int)strlen(p_buf)});

				// 8. 전원 보드 (PB)
				uint8_t pb_id = (uint8_t)dev_status.pwr_ch[0].b_id;
				int pb_len = snprintf(p_buf, 1536, "{\"b_id\":%d,\"pb\":[", pb_id);
				for (int i = 0; i < 8; i++) {
					int sw_stat = (strcmp(dev_status.pwr_ch[i].pwr, "ON") == 0) ? 1 : 0;
					pb_len += snprintf(p_buf + pb_len, 1536 - pb_len,
							"{\"ch\":%d,\"sw\":%d,\"c\":%.2f,\"w\":%.1f,\"v\":%d}%s",
							(i + 1), sw_stat, dev_status.pwr_ch[i].curr,
							dev_status.pwr_ch[i].watt, (int)dev_status.pwr_ch[i].volt,
							(i < 7) ? "," : "");
				}
				snprintf(p_buf + pb_len, 1536 - pb_len, "],\"conn\":1}");
				MQTTPublish(&c, topics.tele_pb, &(MQTTMessage){QOS0, 0, 0, 0, p_buf, (int)strlen(p_buf)});

				// 9. 환기청정기 (AP)
				snprintf(p_buf, 1024, "{\"pwr\":%d,\"mode\":\"%s\",\"spd\":%d,\"uv\":%d,\"co2\":%d,\"pm25\":%d,\"pm10\":%d,\"pm1_0\":%d,\"temp\":%.1f,\"temp_out\":%.1f,\"humi\":%d,\"tvoc\":%d,\"filter\":%d,\"conn\":%d}",
						!strcmp(dev_status.ap.pwr, "ON"), dev_status.ap.mode, dev_status.ap.fan_speed, dev_status.ap.uv_on, dev_status.ap.co2, dev_status.ap.dust_pm25, dev_status.ap.dust_pm10, dev_status.ap.dust_pm1_0, dev_status.ap.temp_in, dev_status.ap.temp_out, dev_status.ap.humi, dev_status.ap.tvoc, dev_status.ap.filter_life, dev_status.ap.is_connected ? 1 : 0);

				MQTTPublish(&c, topics.tele_ap, &(MQTTMessage){QOS0,0,0,0, p_buf, (int)strlen(p_buf)});

				// 10. [v2.0] 외부 입력 4채널
				snprintf(p_buf, 1536,
						"{\"in\":[%d,%d,%d,%d],\"count\":[%lu,%lu,%lu,%lu],\"conn\":1}",
						dev_status.in_stat.p4, dev_status.in_stat.p5,
						dev_status.in_stat.p6, dev_status.in_stat.p7,
						(unsigned long)dev_status.in_stat.cnt4, (unsigned long)dev_status.in_stat.cnt5,
						(unsigned long)dev_status.in_stat.cnt6, (unsigned long)dev_status.in_stat.cnt7);
				MQTTPublish(&c, topics.tele_input, &(MQTTMessage){QOS0,0,0,0, p_buf, (int)strlen(p_buf)});

				// [동기화 핵심] 모든 주기적 보고 타이머를 현재로 갱신하여 즉각적인 중복 보고 방지
				last_th_in_pub_tick  = now;
				last_th_out_pub_tick = now;
				last_dust_pub_tick   = now;
				last_ac_pub_tick     = now;
				last_fan_pub_tick    = now;
				last_pwr_all_pub_tick = now; // PowerBoardTask의 주기를 뒤로 미룸
				last_ap_pub_tick     = now;
				last_input_pub_tick  = now;
				pub_done_pwr_all     = true;
				pub_done_input       = true;

				osMutexRelease(mqtt_mutex_id);
			}
			free(p_buf);
			return;
		}

		/* --- [CASE 3: STATIC IP 값 변경] v2.0 신규
		   ★ 2026-08-10 설계: DHCP/STATIC "모드" 선택은 여전히 config.h의
		   컴파일타임 매크로(SHELTER_NET_USE_STATIC/DHCP)로 결정됩니다.
		   이 명령은 "지금 컴파일된 모드가 STATIC일 때 사용할 IP값"만
		   EEPROM에 저장합니다 — 방금 안정화된 DHCP 콜백 로직은 건드리지
		   않기 위한 의도적인 설계입니다. 보드가 DHCP로 빌드되어 있으면
		   이 값은 저장은 되지만 재빌드 전까지 적용되지 않습니다(정상 동작).
		   규격: {"set_net":{"ip":[192,168,0,50],"sn":[255,255,255,0],"gw":[192,168,0,1],"dns":[8,8,8,8]}}
		   → EEPROM에 저장 후 500ms 뒤 재부팅하여 적용. 재부팅 후 MQTT 연결에
		     성공하면 자동으로 "확정"되고, 실패가 누적되면 이전 설정으로
		     자동 복구됩니다 (net_config.c 참고). */
		else if (strstr(payload, "\"set_net\"") != NULL) {
			NetRuntimeConfig new_cfg = g_net_cfg;   // 브로커 등 나머지 필드는 기존 값 유지
			int ip[4] = {0}, sn[4] = {0}, gw[4] = {0}, dns[4] = {0};
			bool ok = false;

			if (strstr(payload, "\"ip\"") &&
				sscanf(strstr(payload, "\"ip\""),
						"\"ip\":[%d,%d,%d,%d]", &ip[0],&ip[1],&ip[2],&ip[3]) == 4 &&
				strstr(payload, "\"sn\"") &&
				sscanf(strstr(payload, "\"sn\""),
						"\"sn\":[%d,%d,%d,%d]", &sn[0],&sn[1],&sn[2],&sn[3]) == 4 &&
				strstr(payload, "\"gw\"") &&
				sscanf(strstr(payload, "\"gw\""),
						"\"gw\":[%d,%d,%d,%d]", &gw[0],&gw[1],&gw[2],&gw[3]) == 4) {
				for (int i = 0; i < 4; i++) {
					new_cfg.ip[i] = (uint8_t)ip[i];
					new_cfg.sn[i] = (uint8_t)sn[i];
					new_cfg.gw[i] = (uint8_t)gw[i];
				}
				/* dns는 선택 항목 — 없으면 기존 값 유지 */
				if (strstr(payload, "\"dns\"") &&
					sscanf(strstr(payload, "\"dns\""),
							"\"dns\":[%d,%d,%d,%d]", &dns[0],&dns[1],&dns[2],&dns[3]) == 4) {
					for (int i = 0; i < 4; i++) new_cfg.dns[i] = (uint8_t)dns[i];
				}
				ok = true;
			}

			if (ok) {
				NetConfig_SaveAndApply(&new_cfg);
				snprintf(p_buf, 1536, "{\"device\":\"SET_NET\",\"conn\":1,\"result\":1}");
			} else {
				snprintf(p_buf, 1536, "{\"device\":\"SET_NET\",\"conn\":1,\"result\":0}");
			}
			MQTTPublish(&c, topics.stat_dev, &(MQTTMessage){QOS0, 0, 0, 0, p_buf, (int)strlen(p_buf)});

			if (ok) {
				osDelay(500);
				HAL_NVIC_SystemReset();
			}
			free(p_buf);
			return;
		}

		/* --- [CASE 4: MQTT 브로커 IP/포트 변경] v2.0 신규
		   규격: {"set_broker":{"ip":[192,168,0,177],"port":1883}} */
		else if (strstr(payload, "\"set_broker\"") != NULL) {
			NetRuntimeConfig new_cfg = g_net_cfg;
			int ip[4] = {0}, port = 0;
			bool ok = false;

			if (strstr(payload, "\"ip\"") &&
				sscanf(strstr(payload, "\"ip\""), "\"ip\":[%d,%d,%d,%d]",
						&ip[0], &ip[1], &ip[2], &ip[3]) == 4) {
				for (int i = 0; i < 4; i++) new_cfg.broker_ip[i] = (uint8_t)ip[i];
				ok = true;
			}
			if (strstr(payload, "\"port\"") &&
				sscanf(strstr(payload, "\"port\""), "\"port\":%d", &port) == 1 &&
				port > 0 && port <= 65535) {
				new_cfg.broker_port = (uint16_t)port;
			}

			if (ok) {
				NetConfig_SaveAndApply(&new_cfg);
				snprintf(p_buf, 1536, "{\"device\":\"SET_BROKER\",\"conn\":1,\"result\":1}");
			} else {
				snprintf(p_buf, 1536, "{\"device\":\"SET_BROKER\",\"conn\":1,\"result\":0}");
			}
			MQTTPublish(&c, topics.stat_dev, &(MQTTMessage){QOS0, 0, 0, 0, p_buf, (int)strlen(p_buf)});

			if (ok) {
				osDelay(500);
				HAL_NVIC_SystemReset();
			}
			free(p_buf);
			return;
		}

		// 명령어 불일치 시(Result: 0) 처리가 필요한 경우 여기에 else를 추가할 수 있습니다.
		free(p_buf);
		return;
	}

	/* ======================================================================
	       6. [INPUT] 외부 입력 4채널 조회/카운트 초기화 (v2.0 신규)
	       ====================================================================== */
	else if (strstr(topic, topics.cmnd_input) != NULL) {
		char resp[256];

		if (strstr(payload, "\"get_input\"") != NULL) {
			if (osMutexAcquire(mqtt_mutex_id, 200) == osOK) {
				snprintf(resp, sizeof(resp),
						"{\"in\":[%d,%d,%d,%d],\"count\":[%lu,%lu,%lu,%lu],\"conn\":1,\"result\":1}",
						dev_status.in_stat.p4, dev_status.in_stat.p5,
						dev_status.in_stat.p6, dev_status.in_stat.p7,
						(unsigned long)dev_status.in_stat.cnt4, (unsigned long)dev_status.in_stat.cnt5,
						(unsigned long)dev_status.in_stat.cnt6, (unsigned long)dev_status.in_stat.cnt7);
				MQTTPublish(&c, topics.stat_input, &(MQTTMessage){QOS0,0,0,0,resp,(int)strlen(resp)});
				osMutexRelease(mqtt_mutex_id);
			}
		} else if (strstr(payload, "\"reset_count\"") != NULL) {
			if (osMutexAcquire(mqtt_mutex_id, 200) == osOK) {
				dev_status.in_stat.cnt4 = dev_status.in_stat.cnt5 =
						dev_status.in_stat.cnt6 = dev_status.in_stat.cnt7 = 0;
				snprintf(resp, sizeof(resp),
						"{\"in\":[%d,%d,%d,%d],\"count\":[0,0,0,0],\"conn\":1,\"result\":1}",
						dev_status.in_stat.p4, dev_status.in_stat.p5,
						dev_status.in_stat.p6, dev_status.in_stat.p7);
				MQTTPublish(&c, topics.stat_input, &(MQTTMessage){QOS0,0,0,0,resp,(int)strlen(resp)});
				osMutexRelease(mqtt_mutex_id);
			}
		}
		return;
	}

}
