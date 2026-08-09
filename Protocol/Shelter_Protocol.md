# Shelter Protocol (MOS / Mosquitto)

> **규격 개정:** rev **2.0** (tele/state `uid`, 펌웨어 `config.h`)  
> **서버 전달용 엑셀:** `Shelter_Protocol.xlsx` — `python Protocol/_build_shelter_protocol_xlsx.py` 로 재생성  
> **대상 버전:** `MOS_version` (일반 Mosquitto, TLS 미적용)  
> **참고:** `쉘터_MQTT_프로토콜_규격서.xlsx`는 QMEX TLS 버전용 — 본 문서에서는 제외

---

## 1. 개요

Smart Shelter 제어보드(Control Board)가 MQTT 브로커와 JSON 페이로드로 통신하는 규격입니다.

| 항목 | 값 |
|------|-----|
| 장치 식별 | `dev/tele/state`의 **`uid`** (STM32 96-bit UID, 24자리 HEX) |
| LAN 설정 | `Core/Inc/config.h` — **STATIC** 또는 **DHCP** 선택 |
| 브로커 기본 IP | `192.168.0.100` (`config.h`에서 변경) |
| 브로커 포트 | `1883` (MOS, 비암호화) |
| 자동 보고 주기 | **10분** (재연결·명령 응답 시 즉시 1회 + 타이머 리셋) |
| 전원보드 tele | **1초** (연결 감지용, 별도) |
| QoS | 0 (일반) |
| Keepalive | 30초 |
| 페이로드 형식 | JSON (UTF-8) |

### 1.1 토픽 구조

| Prefix | 방향 | 설명 |
|--------|------|------|
| `dev/tele/…` | 장치 → 서버 | Telemetry. 10분 주기 자동 발행 |
| `dev/stat/…` | 장치 → 서버 | `cmnd` 명령 처리 결과 (`result` 포함) |
| `dev/cmnd/…` | 서버 → 장치 | 제어 명령 (장치가 `dev/cmnd/#` 구독) |

> **토픽 prefix:** 본 규격은 `dev/…` 고정 prefix를 사용합니다.  
> (구 테스트 메모의 `dev/{MAC}/…` 형식은 현재 MOS 코드와 불일치)

### 1.2 공통 필드

| 필드 | 타입 | 설명 |
|------|------|------|
| `conn` | int 0/1 | 센서·장치 연결 여부 |
| `result` | int 0/1 | 명령 처리 결과 (`stat` 응답에만). 1=성공, 0=실패 |
| `uid` | string | `dev/tele/state` ONLINE 시 STM32 고유 ID (24자리 HEX). **구 `ip` 필드 대체** |

### 1.3 펌웨어 설정 (`Core/Inc/config.h`)

빌드 전 이 파일만 수정합니다. LAN 모드는 **둘 중 하나만** `1`로 설정합니다.

| 모드 | 설정 | 동작 |
|------|------|------|
| **Static** (기본) | `SHELTER_NET_USE_STATIC 1`, `SHELTER_NET_USE_DHCP 0` | `SHELTER_NET_IP`, `SHELTER_NET_SUBNET`, `SHELTER_NET_GATEWAY`, `SHELTER_NET_DNS` 각 `{a,b,c,d}` |
| **DHCP** | `SHELTER_NET_USE_STATIC 0`, `SHELTER_NET_USE_DHCP 1` | 부팅 시 DHCP, `StartAppTimeTask`에서 `W5500_DhcpTick()` 1초 주기 |

MQTT 브로커: `SHELTER_MQTT_BROKER_IP` `{a,b,c,d}`, `SHELTER_MQTT_BROKER_PORT`.

LAN 주소는 MQTT `tele/state`에 넣지 않습니다. 시리얼 로그 `[NET] Link up. LAN x.x.x.x`로만 확인합니다.

---

## 2. 환경 센서 (Telemetry 전용)

명령(cmnd) 없음. 연결 시 / 미연결 시 페이로드가 다릅니다.

### 2.1 장비 상태 — `dev/tele/state`

| 상태 | Payload |
|------|---------|
| ONLINE | `{"status":"ONLINE","uid":"A1B2C3D4E5F6....","conn":1}` |
| OFFLINE (LWT) | `{"status":"OFFLINE"}` |

- MQTT 연결 시 LWT 설정, retained=1
- 비정상 종료 시 브로커가 OFFLINE 자동 발행

### 2.2 먼지센서 — `dev/tele/dust`

| 상태 | Payload |
|------|---------|
| 연결 | `{"pm1_0":15,"pm2_5":24,"pm10":24,"conn":1}` |
| 미연결 | `{"dust":0,"conn":0}` |

### 2.3 내부 온습도 — `dev/tele/th_in`

| 상태 | Payload |
|------|---------|
| 연결 | `{"temp":25.3,"humi":42.1,"conn":1}` |
| 미연결 | `{"thindoor":0,"conn":0}` |

### 2.4 외부 온습도 — `dev/tele/th_out`

| 상태 | Payload |
|------|---------|
| 연결 | `{"temp":12.5,"humi":35.0,"conn":1}` |
| 미연결 | `{"thoutdoor":0,"conn":0}` |

---

## 3. 자동문 릴레이 (AD, 15채널)

| 구분 | 토픽 |
|------|------|
| tele | `dev/tele/ad` |
| stat | `dev/stat/ad` |
| cmnd | `dev/cmnd/ad` |

### 3.1 Telemetry — `dev/tele/ad`

```json
{"relays":[1,0,0,0,0,0,0,0,0,0,0,0,0,0,0],"conn":1}
```

- `relays`: 15채널 배열, 0=닫힘 / 1=열림
- 보드 부착 상태이므로 미연결 페이로드 없음

### 3.2 Command — `dev/cmnd/ad`

| 명령 | Payload | 설명 |
|------|---------|------|
| 개별 제어 | `{"ch":1,"relay_1":1}` | ch: 1~15, relay_N: 0/1 |
| 전체 제어 | `{"set_ad":"100000000000000"}` | 15자리 0/1 문자열 |
| 상태 조회 | `{"get_ad":"state"}` | 현재 릴레이 상태 요청 |

### 3.3 Stat 응답 — `dev/stat/ad`

```json
{"relays":[1,0,0,0,0,0,0,0,0,0,0,0,0,0,0],"conn":1,"result":1}
```

실패: `"result":0`

---

## 4. 내부 팬 (Fan)

| 구분 | 토픽 |
|------|------|
| tele | `dev/tele/fan` |
| stat | `dev/stat/fan` |
| cmnd | `dev/cmnd/fan` |

### 4.1 Telemetry

| 상태 | Payload |
|------|---------|
| 연결 | `{"mode":"MANUAL","duty":50,"conn":1}` |
| 미연결 | `{"fan":0,"conn":0}` |

- `mode`: `"AUTO"` / `"MANUAL"`
- `duty`: PWM 0~100 (%)
- AUTO: 내부온도 기반 자동 (0~20°C 선형)

### 4.2 Command

| 명령 | Payload |
|------|---------|
| AUTO 모드 | `{"set_fan":{"mode":"AUTO","duty":0}}` |
| MANUAL 모드 | `{"set_fan":{"mode":"MANUAL","duty":50}}` |
| 상태 조회 | `{"get_fan":"state"}` |

### 4.3 Stat 응답

성공:
```json
{"mode":"MANUAL","duty":50,"conn":1,"result":1}
```

미연결:
```json
{"fan":0,"conn":0,"result":1}
```

---

## 5. 전원보드 (PB, 8채널)

Control Board ↔ Power Board는 RS485 바이너리 프로토콜 (`power_board/code/Core/Src/protocol.c`).  
MQTT로 노출되는 PB 데이터는 Control Board가 집계·변환합니다.

| 구분 | 토픽 |
|------|------|
| tele | `dev/tele/pb` |
| stat | `dev/stat/pb` |
| cmnd | `dev/cmnd/pb` |

### 5.1 Telemetry — `dev/tele/pb`

```json
{
  "b_id": 1,
  "pb": [
    {"ch":1,"sw":1,"c":1.14,"w":250.8,"v":220},
    {"ch":2,"sw":1,"c":1.04,"w":228.5,"v":220},
    {"ch":3,"sw":0,"c":0.00,"w":0.0,"v":220},
    {"ch":4,"sw":0,"c":0.00,"w":0.0,"v":220},
    {"ch":5,"sw":1,"c":1.07,"w":235.4,"v":220},
    {"ch":6,"sw":1,"c":1.02,"w":224.4,"v":220},
    {"ch":7,"sw":0,"c":0.00,"w":0.0,"v":220},
    {"ch":8,"sw":1,"c":1.22,"w":268.4,"v":220}
  ],
  "conn": 1
}
```

| 필드 | 설명 |
|------|------|
| `b_id` | 전원보드 ID |
| `ch` | 채널 1~8 |
| `sw` | 0=OFF, 1=ON |
| `c` | 전류 [A] |
| `w` | 전력 [W] |
| `v` | 전압 [V] |

미연결: `{"pb":0,"conn":0}`

> 전원보드는 1초마다 RS485 보고 → Control Board가 연결 상태 판단에 활용

### 5.2 Command

| 명령 | Payload |
|------|---------|
| 개별 채널 | `{"b_id":1,"ch":1,"sw":1}` |
| 전체 일괄 | `{"b_id":1,"set_pb":"11001101"}` (8자리 0/1) |
| 상태 조회 | `{"b_id":1,"get_pb":"state"}` |

### 5.3 Stat 응답

성공: tele과 동일 + `"result":1`  
실패: `{"b_id":1,"pb":0,"conn":1,"result":0}`

---

## 6. 공기청정기 (AP, Himpel)

| 구분 | 토픽 |
|------|------|
| tele | `dev/tele/ap` |
| stat | `dev/stat/ap` |
| cmnd | `dev/cmnd/ap` |

### 6.1 Telemetry

```json
{
  "pwr":1,"mode":"AUTO","spd":3,"uv":1,
  "co2":450,"pm25":15,"pm10":20,"pm1_0":10,
  "temp":23.5,"humi":45,"filter":100,"conn":1
}
```

미연결: `{"ap":0,"conn":0}`

### 6.2 Command (개별)

| Payload | 설명 |
|---------|------|
| `{"pwr":0}` | 전원 0/1 |
| `{"mode":"AUTO"}` | AUTO / MANUAL |
| `{"spd":4}` | 풍속 1~4 |
| `{"uv":1}` | UV 0/1 |
| `{"filter_reset":1}` | 필터 초기화 |
| `{"bypass":1}` | 바이패스 |
| `{"timer":60}` | 타이머 (분) |

### 6.3 Command (일괄 / 조회)

```json
{"set_ap":{"pwr":1,"mode":"AUTO","spd":4,"uv":1,"filter_reset":0,"bypass":0,"timer":0}}
{"get_ap":"state"}
```

### 6.4 Stat 응답

성공: tele + `"result":1`  
실패: `{"ap":0,"conn":0,"result":0}`

---

## 7. 에어컨 (AC, LG Modbus)

| 구분 | 토픽 |
|------|------|
| tele | `dev/tele/ac` |
| stat | `dev/stat/ac` |
| cmnd | `dev/cmnd/ac` |

### 7.1 Telemetry

```json
{"pwr":1,"mode":"COOL","temp":24,"curr":26.0,"spd":3,"err":0,"conn":1}
```

| 필드 | 설명 |
|------|------|
| `mode` | COOL / HEAT / FAN / DRY / AUTO |
| `temp` | 설정온도 [°C] |
| `curr` | 현재온도 [°C] |
| `spd` | 풍속 1~4 |
| `err` | 에러코드 (0=정상) |

미연결: `{"ac":0,"conn":0}`

### 7.2 Command

| Payload | 설명 |
|---------|------|
| `{"pwr":1}` | 전원 |
| `{"mode":"COOL"}` | 운전모드 |
| `{"temp":26}` | 설정온도 |
| `{"spd":3}` | 풍속 |
| `{"set_ac":{"pwr":1,"mode":"COOL","temp":26,"spd":3}}` | 일괄 |
| `{"get_ac":"state"}` | 상태 조회 |

### 7.3 Stat 응답

성공: tele + `"result":1`  
실패: `{"ac":0,"conn":0,"result":0}`

---

## 8. 시스템 제어 (DEV)

| 구분 | 토픽 |
|------|------|
| cmnd | `dev/cmnd/dev` |
| stat | `dev/stat/dev` |

### 8.1 장치 재부팅

**Command**
```json
{"reset":"DEV_RESET"}
```

**Stat 응답**
```json
{"device":"DEV_RESET","conn":0,"result":1}
```

→ 500ms 후 `HAL_NVIC_SystemReset()`

### 8.2 전체 데이터 요청 (ALL_DATA)

**Command**
```json
{"data":"ALL_DATA"}
```

**Stat ACK**
```json
{"device":"ALL_DATA","conn":1,"result":1}
```

이후 아래 tele 토픽 일괄 즉시 발행:

| 토픽 | 예시 |
|------|------|
| `dev/tele/th_in` | `{"temp":23.8,"humi":14.6,"conn":1}` |
| `dev/tele/th_out` | `{"temp":23.3,"humi":20.6,"conn":1}` |
| `dev/tele/dust` | `{"pm1_0":41,"pm2_5":54,"pm10":55,"conn":1}` |
| `dev/tele/ad` | `{"relays":[0,0,...],"conn":1}` |
| `dev/tele/fan` | `{"mode":"AUTO","duty":100,"conn":1}` |
| `dev/tele/ac` | `{"pwr":0,"mode":"COOL","temp":24.0,"curr":0.0,"spd":1,"err":0,"conn":0}` |
| `dev/tele/pb` | `{"b_id":1,"pb":[...],"conn":1}` |
| `dev/tele/ap` | AP tele 형식 |

> 서버 최초 연결·갱신 시 사용. 발행 후 10분 타이머 리셋.

### 8.3 (미구현) 브로커 IP 설정

엑셀에 `{"b_ip":"192.168.0.100"}` 항목이 있으나 **현재 MOS 코드 미구현**. 향후 검토.

---

## 9. 동작 규칙

| # | 규칙 | 설명 |
|---|------|------|
| R1 | 최초 전원 인가 | 각 장비 탐색 → 연결/미연결 형식으로 1회 즉시 발행 → 10분 주기 시작 |
| R2 | 10분 정기 보고 | tele 토픽 순차 발행. PB는 RS485 1초 + MQTT 집계 |
| R3 | 센서 재연결 | 연결 감지 시 1회 즉시 발행 + 10분 타이머 리셋 |
| R4 | 브로커 재연결 | 복구 후 전체 1회 발행 + 타이머 리셋. `ALL_DATA`로 강제 갱신 가능 |
| R5 | result 규칙 | 성공=1+현재상태, 실패=0+기존유지, 범위초과=0 |
| R6 | LWT | 비정상 종료 → `dev/tele/state`에 OFFLINE |
| R7 | 명령 후 타이머 리셋 | stat 응답 성공 시 해당 장비 10분 카운트 리셋 |

---

## 10. MOS_version 코드 매핑

| 구성 | 경로 | 역할 |
|------|------|------|
| Control Board | `MOS_version/control_board/code_mosquitto_choi/` | MQTT 클라이언트, 센서·장비 제어 |
| Power Board | `MOS_version/power_board/code/` | RS485 전원분배 (MOS/TLS 공용) |
| Web Server | `MOS_version/Web_server_mosquitto/` | Node.js + Mosquitto 브로커 + UI |

### 10.1 핵심 소스

| 파일 | 내용 |
|------|------|
| `Core/Src/app.c` | 토픽 정의, MQTT 연결, tele 10분 주기 발행 |
| `Core/Src/mqtt_handler.c` | cmnd 수신 → stat 응답 |
| `Core/Src/app_loop.c` | 센서·릴레이 주기 처리 |
| `Core/Src/phri/power_board.c` | RS485 PB 통신 |
| `power_board/Core/Src/protocol.c` | PB 바이너리 패킷 (STX/ETX/BCC) |
| `Web_server_mosquitto/server.js` | 브로커 구독·UI 연동 |

---

## 11. 규격 ↔ 코드 비교 (MOS_version)

| 항목 | 규격 (본 문서) | 코드 현황 | 비고 |
|------|---------------|----------|------|
| 토픽 prefix | `dev/…` | ✅ 일치 | `app.c` L187~226 |
| AD tele `relays` | 배열 `[0,1,…]` | ✅ 일치 | `app.c` tele 발행 |
| AD stat `relays` | 배열 | ⚠️ **문자열** `"1000…"` | `mqtt_handler.c` L90 — **수정 필요** |
| Fan cmnd 토픽 | `dev/cmnd/fan` | ✅ 동작 | `dev/cmnd/#` 구독으로 수용. 별도 `fan/duty`, `fan/get` 정의는 레거시 |
| PB cmnd | `set_pb`, `get_pb` | ✅ 일치 | `mqtt_handler.c` |
| AP/AC 개별 cmnd | `dev/cmnd/ap`, `/ac` | ✅ 동작 | 페이로드 기반 파싱. `/power`, `/mode` 등 토픽도 정의됨 |
| DEV reset | `{"reset":"DEV_RESET"}` | ✅ 일치 | |
| DEV ALL_DATA | `{"data":"ALL_DATA"}` | ⚠️ PB tele JSON | `mqtt_handler.c` L507~517 **JSON 구조 오류** |
| ONLINE payload | `conn:1` 포함 | ⚠️ `conn` 없음 | `app.c` L379 — 선택적 |
| 브로커 IP 변경 | `b_ip` | ❌ 미구현 | 엑셀 참고 항목 |
| 구 테스트 명령 | `dev/{MAC}/cmnd/…` | ❌ 불일치 | `프로토콜 명령어 모음.txt` 구버전 |

### 11.1 레거시 명령 (구버전 — 사용 금지)

아래는 초기 테스트 메모 형식으로 **현재 MOS 코드와 불일치**:

```
dev/0008DC77631F/cmnd/pb/power     →  dev/cmnd/pb
{"ch":1,"pwr":"ON"}                →  {"b_id":1,"ch":1,"sw":1}
{"pwr_all":"11001100"}             →  {"b_id":1,"set_pb":"11001100"}
{"device":"ALL_DATA"}              →  {"data":"ALL_DATA"}
{"device":"RESET"}                 →  {"reset":"DEV_RESET"}
```

---

## 12. 테스트 예시 (mosquitto CLI)

```bash
# 브로커 구독 (전체 tele)
mosquitto_sub -h 192.168.0.100 -p 1883 -t "dev/tele/#" -v

# PB 채널 1 ON
mosquitto_pub -h 192.168.0.100 -t "dev/cmnd/pb" \
  -m '{"b_id":1,"ch":1,"sw":1}'

# AP 일괄 설정
mosquitto_pub -h 192.168.0.100 -t "dev/cmnd/ap" \
  -m '{"set_ap":{"pwr":1,"mode":"AUTO","spd":3,"uv":1,"filter_reset":0,"bypass":0,"timer":0}}'

# 전체 데이터 요청
mosquitto_pub -h 192.168.0.100 -t "dev/cmnd/dev" \
  -m '{"data":"ALL_DATA"}'

# 장치 리셋
mosquitto_pub -h 192.168.0.100 -t "dev/cmnd/dev" \
  -m '{"reset":"DEV_RESET"}'
```

---

## 부록 A — 원본 엑셀 오타 정정

`쉘터 프로토콜 정의.xlsx` 작성 시 JSON 오타를 본 문서에서 정정했습니다:

| 원본 (오타) | 정정 |
|------------|------|
| `{"set_ad","100000000000000"}` | `{"set_ad":"100000000000000"}` |
| `{"fan",0,"conn":0}` | `{"fan":0,"conn":0}` |
| `{"b_id":1,"set_pb","00000000"}` | `{"b_id":1,"set_pb":"00000000"}` |
| `{"b_id",1,get_pb","state"}` | `{"b_id":1,"get_pb":"state"}` |
| `{"get_ad","state"}` | `{"get_ad":"state"}` |
| `{"set_fan",{"mode":…}}` | `{"set_fan":{"mode":…}}` |

---

## 부록 B — 향후 (TLS / QMEX)

- `TLS_version/` — QMEX TLS 브로커 (`8883`, mbedTLS)
- `쉘터_MQTT_프로토콜_규격서.xlsx` — TLS 버전 상세 규격
- `power_board` 코드는 MOS/TLS **공용**
- TLS 통합 시 브로커·인증만 변경, MQTT JSON 규격은 동일 유지 예정
