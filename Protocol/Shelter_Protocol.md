# Shelter Protocol (MOS / Mosquitto)

> **버전 이력**
> | 버전 | 날짜 | 내용 |
> |------|------|------|
> | v1.0 | ~2026-08-08 | `tele/state`에 `uid` 도입 (구 `ip` 필드 대체). 아래 문서 원문(섹션 1~12, 부록 A/B)이 v1.0 기준입니다. |
> | **v2.0** | **2026-08-10** | **① 외부 입력 4채널(PD4~7) ② STATIC IP/브로커 Flash 저장 + CMS 유지보수 화면 ③ DHCP↔MQTT 소켓 충돌·RS485 무한대기 등 재접속 안정성 ④ `tele/state`에 `ip`/`mode` 필드.** |
> | **v2.1** | **2026-08-12** | **① PC 이더넷 직결 프로비저닝 (`192.168.0.100`) — 양산·현장 최초 설정 표준 ② 펌웨어 1종 + Flash 저장 ③ CMS 네트워크 메뉴는 유지보수 전용으로 역할 분리.** |
> | **v2.2** | **2026-08-12** | **① 장치별 `conn`(연결됨/오프라인) 판정 조건 명문화 (펌웨어 + CMS) ② 팬 tele에 `rpm` 추가 ③ PB tele 주기·CMS stale 보정.** |
| **v2.3** | **2026-08-13** | **① PB `b_id` = 전원보드 DIP 4bit(0~15) 실측값 — 고정 ID 아님 ② 서버·CMS는 tele/get_pb의 `b_id` 캐시 후 cmnd에 사용.** |
| **v2.4** | **2026-08-13** | **① AP tele `temp_out`·`tvoc`·확장 mode·spd 0~4 ② AC spd 1~6 ③ tele/pb `result` 제거 ④ AC/AP 복합 cmnd ⑤ CMS PB·스케줄 정합.** |
| **v2.5** | **2026-08-13** | **① 환경센서 3종(UP-DM010UB/HC-SD I2C/CWT Modbus) 문서 정리 ② DEV_RESET=소프트 리부트 ③ PB 펌웨어 릴레이 간격 200ms ④ stat/pb 실패 conn 규칙 ⑤ CMS PB b_id 학습 후만 cmnd.** |
>
> **서버 전달용 엑셀:** `Shelter_Protocol.xlsx` / `Shelter_Protocol_v1.0.xlsx` — `python Protocol/_build_shelter_protocol_xlsx.py` 로 재생성  
> **서버·연동 개발자용:** `개발가이드.docx` / `개발가이드_v1.0.docx` — `python Protocol/_build_개발가이드_docx.py` 로 재생성  
> **설치 업체용 가이드:** `Shelter_Installation_Guide.md` (현장·공장 설치 절차)  
> **대상 버전:** `MOS_version` (일반 Mosquitto, TLS 미적용)  
> **참고:** `쉘터_MQTT_프로토콜_규격서.xlsx`는 QMEX TLS 버전용 — 본 문서에서는 제외

---

## 1. 개요

Smart Shelter 제어보드(Control Board)가 MQTT 브로커와 JSON 페이로드로 통신하는 규격입니다.

| 항목 | 값 |
|------|-----|
| 장치 식별 | `dev/tele/state`의 **`uid`** (STM32 96-bit UID, 24자리 HEX) |
| **최초 LAN/MQTT 설정** | **PC 직결 HTTP** `http://192.168.0.100` (섹션 **2C**, 설치 가이드 참고) |
| LAN 모드 (현장) | Flash 저장 — **DHCP 권장** 또는 STATIC |
| 브로커 | Flash 저장 — 직결 설정 또는 CMS 유지보수 / MQTT `set_broker` |
| `config.h` | Flash 비어 있을 때만 쓰는 **공장 출하 기본값** (펌웨어 1종 양산) |
| 브로커 포트 | `1883` (MOS, 비암호화) |
| 자동 보고 주기 | **10분** (재연결·명령 응답 시 즉시 1회 + 타이머 리셋) |
| 전원보드 tele | **10초** (RS485 폴링 ~0.8s, MQTT 집계) |
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
| `conn` | int 0/1 | 센서·장치 연결 여부 (장치별 판정 — **섹션 1.4**) |
| `result` | int 0/1 | 명령 처리 결과 (`stat` 응답에만). 1=성공, 0=실패 |
| `uid` | string | `dev/tele/state` ONLINE 시 STM32 고유 ID (24자리 HEX). **구 `ip` 필드 대체** |

### 1.3 설정 우선순위 (v2.1)

| 순위 | 저장 위치 | 용도 |
|------|-----------|------|
| 1 | **내부 Flash** (`net_config.c`) | LAN 모드(DHCP/STATIC), IP/SN/GW/DNS, 브로커 IP/포트 — **직결 HTTP 또는 CMS/MQTT로 기록** |
| 2 | **`config.h` 컴파일 기본값** | Flash erase·공장 초기화 후, 또는 최초 부팅 전까지의 fallback |

**양산·현장 최초 설정**은 CMS가 아니라 **섹션 2C 직결 HTTP**를 사용합니다.  
**CMS 네트워크 메뉴**와 MQTT `set_net` / `set_broker`는 **이미 ONLINE인 장치 유지보수**용입니다.

`config.h` (`Core/Inc/config.h`) 공장 출하 기본:

| 항목 | 공장 기본 |
|------|-----------|
| LAN 모드 | **DHCP** (`SHELTER_NET_USE_DHCP 1`) |
| STATIC fallback IP | `192.168.0.50` / `255.255.255.0` / GW `192.168.0.1` |
| 브로커 fallback | `SHELTER_MQTT_BROKER_IP` (빌드 시 1회, 예: `192.168.0.107`) |
| DHCP 소켓 | `SHELTER_DHCP_SOCKET_NUM 3` (MQTT 소켓 0과 분리) |

LAN 주소는 시리얼 `[NET] Link up. LAN x.x.x.x` 또는 `dev/tele/state`의 `ip` 필드로 확인합니다.

### 1.4 `conn` — 연결됨 / 오프라인 판정 (v2.2)

CMS UI의 **연결됨** 배지는 **각 MQTT tele/stat의 `conn` 필드**를 따릅니다.  
메인보드 MQTT 메시지가 온다고 **하위 장치가 모두 연결됨으로 바뀌지 않습니다** (v2.2 CMS 수정).

#### 1.4.1 펌웨어 (Control Board) 판정

| 장치 | `conn:1` 조건 | `conn:0` / 미연결 payload | tele 주기 |
|------|---------------|---------------------------|-----------|
| **state** (메인보드) | MQTT 브로커 연결 성공 | LWT `{"status":"OFFLINE"}` | 연결/해제 |
| **dust** | UART 미세먼지 패킷 수신, **60초 이내** 갱신 | 동일 스키마 + `conn:0` | 10분 (+변경 시) |
| **th_in** | HC-SD **I2C** 읽기 성공 + 온도 ≠ 0±0.1 | 동일 스키마 + `conn:0` | 10분 |
| **th_in** (끊김) | — | 연속 **30회** 읽기 실패 (~3s×30≈90s) | — |
| **th_out** | CWT-TH03S Modbus 읽기 성공 + 유효 온도 | 동일 스키마 + `conn:0` | 10분 |
| **th_out** (끊김) | — | 연속 **10회** 실패 (~2.5s×10≈25s) | — |
| **ad** | 제어보드 GPIO 릴레이 (항상 부착) | 미연결 payload 없음 | 10분 |
| **fan** | duty **≥10%**: TIM1 CH3 타코 펄스 **7초 이내** | `{"fan":0,"rpm":0,"conn":0}` | 10분 / conn 변경 |
| **fan** (저속) | duty **<10%**: 최근 **60초** 내 타코 이력 (배선 확인) | (동일) | — |
| **pb** | RS485 `PowerBoard_UpdateAllData` **HAL_OK** | `{"b_id":N,"pb":0,"conn":0}` | **10초** |
| **pb** (끊김) | — | RS485 **연속 10회** 실패 (~0.8s×10≈8s) | — |
| **ap** | Himpel RS485 응답 OK (`getFanStatus`) | `{"ap":0,"conn":0}` | 10분 |
| **ap** (끊김) | — | 통신 실패 또는 부팅 후 **10초** 무응답 | — |
| **ac** | Modbus **Discrete** `ADDR_AC_CONN` 읽기 성공 **且** 비트=1 | `{"ac":0,"conn":0}` | 10분 / ~1s 폴링 |
| **ac** (끊김) | — | Discrete 읽기 실패 또는 conn 비트=0 | — |
| **input** | PD4~PD7 GPIO (보드 부착) | 미연결 payload 없음 | 변경 즉시 |

#### 1.4.2 CMS (Web_server_mosquitto) 표시

| 항목 | 규칙 |
|------|------|
| **기본** | tele/stat JSON의 `conn` 값만 사용 (`normalizeConn`) |
| **메인보드 생존** | **아무 MQTT 메시지** 수신 시 `state.conn=1`, **5분** 타이머 리셋 |
| **메인보드 OFFLINE** | 5분간 MQTT 없음 → `state` OFFLINE + **dust/th/ad/fan/ap/ac/input conn=0** |
| **PB CMS** | `pb[]` 배열 + `conn:1` → conn·전력 그래프 반영; `pb:0` 또는 배열 없음 → conn=0. **tele에는 `result` 없음** (stat 전용). stale **35초** |
| **stat 실패** | `result:0` → 해당 장치 `conn=0` (stat 응답 기준) |
| **PB b_id** | CMS 초기값 **0** — `tele/pb` 수신 후 실측 `b_id` 캐시. 미학습 시 PB cmnd·스케줄 PB 생략 |
| **스케줄/일괄** | AC/AP 출근 시 **`set_ac` / `set_ap`** 권장 (복합 JSON도 펌웨어 v2.4에서 지원) |

#### 1.4.3 팬 tele (v2.2)

연결 시 예시:

```json
{"mode":"MANUAL","duty":50,"rpm":1234,"conn":1}
```

- `rpm`: TIM1 CH3 타코 주파수 환산 (정수, 회/min)
- duty 0% 근처에서도 인버터 배선만 연결되어 있으면 60초 이내 회전 이력으로 `conn:1` 가능

---

## 2. 환경 센서 (Telemetry 전용)

명령(cmnd) 없음. 연결 시 / 미연결 시 페이로드가 다릅니다.

### 2.1 장비 상태 — `dev/tele/state`

| 상태 | Payload |
|------|---------|
| ONLINE (v1.0) | `{"status":"ONLINE","uid":"A1B2C3D4E5F6....","conn":1}` |
| ONLINE (**v2.0**) | `{"status":"ONLINE","uid":"A1B2C3D4E5F6....","ip":"192.168.0.115","mode":"DHCP","conn":1}` |
| OFFLINE (LWT) | `{"status":"OFFLINE"}` |

- MQTT 연결 시 LWT 설정, retained=1
- 비정상 종료 시 브로커가 OFFLINE 자동 발행
- **v2.0 추가:** `ip`(현재 실제 IP), `mode`(`"DHCP"`/`"STATIC"`, Flash 저장값) — CMS 유지보수 화면에서 표시

### 2.2 먼지센서 (UP-DM010UB, UART) — `dev/tele/dust`

| 상태 | Payload |
|------|---------|
| 연결 | `{"pm1_0":15,"pm2_5":24,"pm10":24,"conn":1}` |
| 미연결 | `{"pm1_0":0,"pm2_5":0,"pm10":0,"conn":0}` — **필드 형식 동일**, `conn:0`으로 판정 |

> 구버전 문서의 `{"dust":0,"conn":0}` 형식은 **현재 펌웨어 미사용**. 서버는 **`conn`** 기준으로 오프라인 처리.

### 2.3 내부 온습도 (HC-SD, I2C) — `dev/tele/th_in`

| 상태 | Payload |
|------|---------|
| 연결 | `{"temp":25.3,"humi":42.1,"conn":1}` |
| 미연결 | `{"temp":0.0,"humi":0.0,"conn":0}` — **필드 형식 동일** |

> HC-SD는 **I2C**(HumiChip2), Modbus 아님. 연속 30회 읽기 실패(~90s) 시 `conn:0`.

### 2.4 외부 온습도 (CWT-TH03S, Modbus RTU) — `dev/tele/th_out`

| 상태 | Payload |
|------|---------|
| 연결 | `{"temp":12.5,"humi":35.0,"conn":1}` |
| 미연결 | `{"temp":0.0,"humi":0.0,"conn":0}` — **필드 형식 동일** |

> 연속 10회 Modbus 실패(~25s) 시 `conn:0`.

---

## 2A. [v2.0 신규] 외부 입력 4채널 — `dev/tele/input` / `dev/cmnd/input` / `dev/stat/input`

STM32 GPIOD PD4~PD7 4개 디지털 입력. `SCAN_External_Inputs()`가 매 센서 태스크 루프
(~200ms)마다 스캔하며, **상태 변경 시 10분을 기다리지 않고 즉시 1회 발행**합니다
(다른 센서와 동일한 R3 규칙). 채널별로 상승엣지(신호 없음→있음) 횟수를 누적 카운트합니다.

| 구분 | 토픽 |
|------|------|
| tele | `dev/tele/input` |
| stat | `dev/stat/input` |
| cmnd | `dev/cmnd/input` |

### 2A.1 Telemetry — `dev/tele/input`

```json
{"in":[1,0,0,1],"count":[12,0,0,3],"conn":1}
```

| 필드 | 설명 |
|------|------|
| `in[0..3]` | PD4, PD5, PD6, PD7 현재 상태 (0=신호 없음, 1=신호 있음) |
| `count[0..3]` | 각 채널의 누적 상승엣지 카운트 (부팅 이후, 재부팅 시 0으로 초기화) |

- 웹서버 UI: 4개 원(●) — 흰색=신호없음, 초록색=신호있음, 원 아래에 count 숫자 표시
- **자동 보고**: PD4–7 상태 변경 시 즉시 `dev/tele/input` 발행. 주기 보고(약 10분)에도 포함.
- **서버**: `get_input` 주기 폴링 없음 — 제어보드 tele/stat만 수신 처리.

### 2A.2 Command — `dev/cmnd/input`

| 명령 | Payload | 설명 |
|------|---------|------|
| 상태 조회 (수동) | `{"get_input":"state"}` | 필요 시 1회 조회 — stat 응답. 서버는 자동 전송하지 않음 |
| 카운트 초기화 | `{"reset_count":1}` | 4채널 카운트를 0으로 리셋 (입력 상태 자체는 유지) |

### 2A.3 Stat 응답 — `dev/stat/input`

```json
{"in":[1,0,0,1],"count":[12,0,0,3],"conn":1,"result":1}
```

---

## 2B. [v2.0] 네트워크/브로커 설정 — 유지보수용 (`set_net` / `set_broker`)

> **최초 설치·양산:** 섹션 **2C** 직결 HTTP 사용. 본 절은 **이미 LAN/MQTT에 붙은 장치**의 원격 변경용입니다.

**설계 원칙:** DHCP/STATIC 모드, IP/SN/GW/DNS, 브로커 IP/포트는 **내부 Flash**에 저장됩니다
(`net_config.c`, Bank2 Sector7). CMS 유지보수 메뉴 또는 아래 MQTT 명령으로 변경 가능합니다.

1. **LAN 모드 및 STATIC IP/SN/GW/DNS**
2. **MQTT 브로커 IP/포트** (DHCP/STATIC 어느 모드든 재부팅 후 적용)

### 2B.1 STATIC IP값 변경

**Command** (`dev/cmnd/dev`)
```json
{"set_net":{"ip":[192,168,0,50],"sn":[255,255,255,0],"gw":[192,168,0,1],"dns":[8,8,8,8]}}
```
- `dns`는 선택 항목 (없으면 기존 값 유지)

**Stat 응답** (`dev/stat/dev`)
```json
{"device":"SET_NET","conn":1,"result":1}
```
→ 성공 시 500ms 후 재부팅하여 적용 시도

### 2B.2 브로커 IP/포트 변경

**Command**
```json
{"set_broker":{"ip":[192,168,0,177],"port":1883}}
```
- `port`는 선택 항목 (없으면 기존 포트 유지)

**Stat 응답**
```json
{"device":"SET_BROKER","conn":1,"result":1}
```
→ 성공 시 500ms 후 재부팅하여 적용 시도

### 2B.3 DHCP Flash Reset — `set_net_reset`

STATIC IP를 EEPROM/Flash에 저장해 사용하다가 다시 DHCP로 돌아가고 싶을 때 사용합니다.

**Command**
```json
{"set_net_reset":1}
```

**Stat 응답**
```json
{"device":"SET_NET_RESET","conn":1,"result":1}
```
→ `net_mode = 1` (DHCP) 저장 후 재부팅

웹 UI: **유지보수 → Flash Reset → DHCP 복귀** (최초 설정에는 사용하지 않음)

### 2B.4 Flash 설정 삭제 (펌웨어 예정) — `clear_net` / `clear_broker`

자체 8채널 스위치 등 **별도 제어보드**에서 저장된 IP/브로커 설정을 지우고
`config.h` 컴파일타임 기본값으로 되돌릴 때 사용 (향후 펌웨어 구현 예정).

| 명령 | Payload | 동작 (예정) |
|------|---------|-------------|
| LAN 설정 삭제 | `{"clear_net":1}` | Flash IP/SN/GW/DNS 삭제 → config.h 기본값 |
| 브로커 설정 삭제 | `{"clear_broker":1}` | Flash broker_ip/port 삭제 → config.h `SHELTER_MQTT_BROKER_IP` |

**Stat 응답 (예정)**
```json
{"device":"CLEAR_NET","conn":1,"result":1}
{"device":"CLEAR_BROKER","conn":1,"result":1}
```

> ⚠️ `clear_*` 명령은 **자체 스위치용 별도 제어보드** 전용입니다. 메인 쉘터 제어보드는
> `set_net` / `set_broker` / `set_net_reset`을 사용하세요.

### 2B.5 안전장치 (자동 롤백)

새 설정 적용 후 60초(`SHELTER_NET_ROLLBACK_TIMEOUT_MS`) 안에 MQTT 연결에 성공하지
못하면 재부팅하며, 이런 미확정 재부팅이 3회(`SHELTER_NET_ROLLBACK_MAX_FAILS`) 누적되면
**자동으로 바로 이전(정상 동작 확인된) 설정으로 복구**합니다. 잘못된 IP를 입력해도
현장에서 보드가 완전히 응답 불가 상태로 남지 않도록 하기 위함입니다.

> ⚠️ EEPROM(25LC256) 경로는 이번에 처음 실제로 사용됩니다. 현장 배포 전 최소 1회는
> "설정 변경 → 재부팅 → 값이 실제로 반영되는지"를 직접 확인해 주세요. EEPROM이 없거나
> 응답하지 않으면 `NetConfig_Load()`가 자동으로 `config.h` 컴파일타임 값으로 대체되어
> 보드가 멈추지 않고 안전하게 부팅됩니다.

---

> Flash 저장 경로는 현장 배포 전 최소 1회 "직결 설정 → 재부팅 → MQTT ONLINE"을 확인해 주세요.
> Flash가 비어 있거나 checksum 오류면 `NetConfig_Load()`가 `config.h` 기본값으로 대체되어
> 보드가 멈추지 않고 안전하게 부팅됩니다.

---

## 2C. [v2.1] PC 이더넷 직결 프로비저닝 — **양산·현장 최초 설정 표준**

MQTT 브로커에 **최초 연결하지 못한 상태**에서, PC와 이더넷 케이블로 직접 연결해
제어보드 **내장 HTTP 페이지**로 LAN/MQTT를 설정합니다.

- **XML·별도 PC 툴 불필요** — Flash에 HTML 내장
- **펌웨어 1종** — 고객/현장별 `config.h` 재빌드 불필요
- **설치 절차 상세:** `Protocol/Shelter_Installation_Guide.md`

### 2C.1 진입 조건

| 상황 | 조건 |
|------|------|
| 공장 / Flash 없음 | 링크 UP + IP 없음 **5초** (`SHELTER_PROV_LINKUP_MS`) 또는 MQTT **90초** 실패 |
| Flash 있음, **재설정** (`pending=0`) | 링크 UP + IP 없음 **5초** → `192.168.0.100` |
| **저장 직후 1회** (`pending=1`) | LAN DHCP **최대 2분** (`SHELTER_PROV_AFTER_SAVE_MS`) — PC 직결 유지 시 prov **지연** (케이블을 공유기 LAN으로 교체) |
| 케이블 미연결 | prov 진입 **불가** — `[WAIT] Cable down` 로그 |

저장 시 Flash `pending=1` → MQTT ONLINE 시 `pending=0` 확정 (`NetConfig_ConfirmBoot`).

### 2C.2 IP 주소

| 장치 | IP | 서브넷 | 비고 |
|------|-----|--------|------|
| **제어보드** | **192.168.0.100** | 255.255.255.0 | GW 192.168.0.1 (직결 전용) |
| **PC (설정용)** | **192.168.0.10** | 255.255.255.0 | 같은 대역 unused IP 가능 |
| **브라우저** | `http://192.168.0.100` | — | 포트 80 |

### 2C.3 설정 페이지 (내장 HTML)

| 항목 | 권장 (일반 현장) | 비고 |
|------|------------------|------|
| LAN 모드 | **DHCP** | 공유기 LAN에 연결 후 IP 자동 획득 |
| STATIC | IP/SN/GW/DNS | 고정 IP 현장만 |
| MQTT 브로커 IP | **관제 PC LAN IP** | 예: `192.168.0.107` |
| MQTT 포트 | **1883** | MOS 비암호화 |

**[저장 및 재부팅]** → Flash 저장 (`pending=1`) → 재부팅 → **PC 직결 해제** → 현장 LAN 연결 → MQTT 접속 → `pending=0` 확정

> DHCP 모드: IP 입력란은 무시됨. 고정 IP는 **STATIC** 선택.

### 2C.4 양산 공정 (권장)

```
[1] 공통 펌웨어 플래시
[2] PC ↔ 제어보드 이더넷 직결 (90초 이내 또는 MQTT 실패 대기)
[3] PC NIC 192.168.0.10 설정 → http://192.168.0.100
[4] LAN=DHCP, 브로커=양산 서버 IP → 저장 및 재부팅
[5] 현장 LAN(또는 공장 테스트 LAN) 연결 → CMS에서 uid ONLINE 확인
[6] 라벨(UID) 부착 → 출하
```

### 2C.5 현장 설치 (설치 업체)

1. 쉘터 전원 ON, 이더넷을 **관제 PC/공유기 LAN**에 연결 (직결 설정 완료 장치는 DHCP로 IP 획득)
2. CMS 웹에서 장치 **ONLINE** 및 `uid` 확인
3. 미연결 시: PC 직결 → 2C.2~2C.3 반복 (설치 가이드 참고)

### 2C.6 공장 초기화

| 방법 | 동작 |
|------|------|
| 물리 **SW8** | Flash erase → DHCP + config.h 기본값 |
| MQTT `{"set_net_reset":1}` | 동일 (유지보수) |

### 2C.7 운영 중 원격 변경 (선택)

LAN 연결 후 **CMS 유지보수 메뉴** 또는 MQTT `set_net` / `set_broker` — 섹션 2B.

---

## 3. AD (릴레이 15채널)

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
| 연결 | `{"mode":"MANUAL","duty":50,"rpm":1234,"conn":1}` |
| 미연결 | `{"fan":0,"rpm":0,"conn":0}` |

- `mode`: `"AUTO"` / `"MANUAL"`
- `duty`: PWM 0~100 (%)
- `rpm`: 타코 측정 회전수 (정수). **v2.2 추가**
- AUTO: 내부온도 기반 자동 (0~20°C 선형)
- **conn**: duty≥10% → 7초 내 타코 펄스; duty<10% → 60초 내 이력 (섹션 1.4)

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

### 5.0 전원보드 ID (`b_id`)

**고정값이 아닙니다.** `b_id`는 전원보드(PB) 앞단 **4개 슬라이드 DIP 스위치**로 결정되는 **4bit 보드 주소 (0~15)** 입니다.

| 단계 | 동작 |
|------|------|
| **전원보드** | 부팅 시 `HW_Get_BoardID()` — ID1~ID4 핀 (ON=LOW → 해당 bit 1). RS485 송·수신 패킷 **byte[1]**에 ID 포함 |
| **제어보드** | RS485 수신 → `PowerBoard_UpdateAllData()`가 패킷 byte[1]을 `dev_status.pwr_ch[].b_id`에 저장 |
| **MQTT** | `dev/tele/pb` · `dev/stat/pb`의 `b_id` = 위에서 **학습한 실측값** |
| **서버 → 제어** | `dev/cmnd/pb`의 `b_id`는 tele 또는 `get_pb` 응답과 **일치**해야 함 (불일치 → `result:0`) |

- **쉘터 현장:** PB **1대**만 RS485 연결 (일반 구성).
- **다보드 확장:** DIP를 달리한 PB 추가 시 버스上 `b_id`가 여러 개 공존 가능.
- **서버 권장:** `b_id=1` 등 **하드코딩 금지** — `tele/pb` 또는 `get_pb`의 `b_id`를 캐시해 `set_pb`·개별 채널 제어에 사용.
- **레거시:** `{"id":N,...}` — `id` ≡ `b_id`.

> DIP bit: ID1=bit0(LSB), ID2=bit1, ID3=bit2, ID4=bit3(MSB). 스위치 ON(LOW)=1.  
> 예: ID1만 ON → `b_id=1`, ID1·ID3 ON → `b_id=5`.

### 5.1 Telemetry — `dev/tele/pb`

```json
{
  "b_id": 3,
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
| `b_id` | 전원보드 DIP 4bit 실측 ID (**0~15**, tele/get_pb에서 확인·cmnd에 동일값 사용) |
| `ch` | 채널 1~8 |
| `sw` | 0=OFF, 1=ON |
| `c` | 전류 [A] |
| `w` | 전력 [W] |
| `v` | 전압 [V] |

미연결: `{"pb":0,"conn":0}`

> RS485 폴링 ~0.8s, MQTT tele **10초**. 연속 10회 RS485 실패 시 conn=0. CMS는 tele 35초 stale 시 그래프 offline.

### 5.2 Command

| 명령 | Payload |
|------|---------|
| 개별 채널 | `{"b_id":N,"ch":1,"sw":1}` — **N = tele/get_pb의 실측 `b_id`** |
| 전체 일괄 | `{"b_id":N,"set_pb":"11001101"}` (8자리 0/1) |
| 상태 조회 | `{"b_id":N,"get_pb":"state"}` |

### 5.3 Stat 응답

성공: tele과 동일 + `"result":1`  
실패: `{"b_id":N,"pb":0,"conn":1,"result":0}` — PB **온라인**이나 b_id 불일치 등 (v2.5)  
실패(PB 미연결): `{"b_id":N,"pb":0,"conn":0,"result":0}`

---

## 6. 공기청정기 (AP, Himpel RS485)

| 구분 | 토픽 |
|------|------|
| tele | `dev/tele/ap` |
| stat | `dev/stat/ap` |
| cmnd | `dev/cmnd/ap` |

> **통신:** Himpel 제조사 **RS485 전용 프레임** (Modbus RTU 아님). 제어보드 `himpel.c`.

### 6.1 Telemetry

```json
{
  "pwr":1,"mode":"AUTO","spd":3,"uv":1,
  "co2":450,"pm25":15,"pm10":20,"pm1_0":10,
  "temp":23.5,"temp_out":18.2,"humi":45,"tvoc":120,
  "filter":100,"conn":1
}
```

| 필드 | 설명 |
|------|------|
| `temp` | 실내 온도 [°C] |
| `temp_out` | 실외(급기) 온도 [°C] |
| `tvoc` | TVOC [ppb] |
| `spd` | 풍속 **0~4** (0=정지, 4=터보) |
| `mode` | AUTO / MANUAL / CLEAN / VENT / BYPASS / HEATER |

미연결: `{"ap":0,"conn":0}`

### 6.2 Command (개별·복합)

한 JSON에 **여러 필드를 동시에** 넣을 수 있습니다 (v2.4). 펌웨어는 포함된 필드를 순차 적용합니다.

| Payload | 설명 |
|---------|------|
| `{"pwr":0}` | 전원 0/1 |
| `{"mode":"AUTO"}` | AUTO / MANUAL / CLEAN / VENT / BYPASS / HEATER |
| `{"spd":4}` | 풍속 **0~4** |
| `{"uv":1}` | UV 0/1 |
| `{"filter_reset":1}` | 필터 초기화 |
| `{"bypass":1}` | 바이패스 |
| `{"timer":60}` | 타이머 (0~255) |
| `{"pwr":1,"mode":"AUTO","spd":2}` | 복합 예 (v2.4) |

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
| `spd` | 풍속 **1~6** (LG 실내기 풍량 단계) |
| `err` | 에러코드 (0=정상) |

미연결: `{"ac":0,"conn":0}`

### 7.2 Command (개별·복합)

한 JSON에 **pwr·mode·temp·spd를 동시에** 지정 가능 (v2.4). 스케줄·일괄 제어는 **`set_ac`** 권장.

| Payload | 설명 |
|---------|------|
| `{"pwr":1}` | 전원 |
| `{"mode":"COOL"}` | COOL / HEAT / FAN / DRY / AUTO |
| `{"temp":26}` | 설정온도 16~30°C |
| `{"spd":3}` | 풍속 **1~6** |
| `{"pwr":1,"mode":"COOL","temp":24,"spd":2}` | 복합 예 (v2.4) |
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

→ 500ms 후 **`HAL_NVIC_SystemReset()`** (소프트 리부트, **Flash·네트워크 설정 유지**)

> **주의:** Flash·DHCP **공장 초기화**는 **`{"set_net_reset":1}`** (섹션 2B) — DEV_RESET과 다름.  
> (v2.5 이전 펌웨어는 DEV_RESET이 Flash erase였음 — **반드시 v2.5+ 펌웨어 사용**)

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
| `dev/tele/fan` | `{"mode":"AUTO","duty":100,"rpm":2100,"conn":1}` |
| `dev/tele/ac` | `{"pwr":0,"mode":"COOL","temp":24.0,"curr":0.0,"spd":1,"err":0,"conn":0}` |
| `dev/tele/pb` | `{"b_id":N,"pb":[...],"conn":1}` — **N** = DIP 실측 |
| `dev/tele/ap` | AP tele 형식 |

> 서버 최초 연결·갱신 시 사용. 발행 후 10분 타이머 리셋.

### 8.3 [v2.0] 네트워크/브로커 설정

**v1.0에서는 미구현**이었으나(`{"b_ip":"192.168.0.100"}` 엑셀 항목), **v2.0에서 구현**되었습니다.
`dev/cmnd/dev`의 `set_net` / `set_broker` 명령을 사용합니다 — 상세 규격은 **섹션 2B** 참고.

---

## 9. 동작 규칙

| # | 규칙 | 설명 |
|---|------|------|
| R1 | 최초 전원 인가 | 각 장비 탐색 → 연결/미연결 형식으로 1회 즉시 발행 → 10분 주기 시작 |
| R2 | 10분 정기 보고 | tele 토픽 순차 발행. PB는 RS485 ~0.8s 폴링 + MQTT **10초** | |
| R3 | 센서 재연결 | 연결 감지 시 1회 즉시 발행 + 10분 타이머 리셋 | |
| R4 | 브로커 재연결 | 복구 후 전체 1회 발행 + 타이머 리셋. `ALL_DATA`로 강제 갱신 가능 | |
| R5 | result 규칙 | 성공=1+현재상태, 실패=0+기존유지, 범위초과=0 | |
| R6 | LWT | 비정상 종료 → `dev/tele/state`에 OFFLINE | |
| R7 | 명령 후 타이머 리셋 | stat 응답 성공 시 해당 장비 10분 카운트 리셋 | |
| **R10** | **conn 독립 (v2.2)** | **장치별 tele `conn`만 UI 반영. 메인보드 MQTT 수신 ≠ 전체 연결됨** | CMS `server.js` |
| **R11** | **CMS 메인 타임아웃** | **5분간 보드 MQTT 없음 → state OFFLINE + 하위 conn 0 (pb 제외 키)** | `DEVICE_TIMEOUT_MS` |
| **R12** | **장치별 conn** | dust 60s / fan 타코 7s·60s / PB RS485×10 / AC discrete / AP 10s — **섹션 1.4** | 펌웨어 |
| **R13** | **복합 cmnd (v2.4)** | AC/AP cmnd JSON에 여러 키 동시 포함 시 **포함 필드 모두 순차 적용**. 일괄은 `set_ac`/`set_ap` 권장 | `mqtt_handler.c` |
| **R14** | **tele vs stat (v2.4)** | **`result`는 stat 전용**. tele/pb 등 tele 토픽에는 `result` 없음 | `app.c`, CMS |
| **R15** | **PB b_id (v2.4)** | 서버 `b_id` **하드코딩 금지**. tele 수신 전 PB cmnd·스케줄 PB **생략** (`lastPbDataValid`) | `server.js` |
| **R16** | **PB 릴레이 순차 (v2.5)** | 전원보드 0xAA 처리: 채널당 상태 변경 시 **200ms** (돌입전류 방지) | `power_board/code/app.c` |

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
| AP/AC 개별 cmnd | `dev/cmnd/ap`, `/ac` | ✅ 동작 | v2.4: 한 JSON 다필드 지원. `/power`, `/mode` 서브토픽은 레거시 |
| PB tele `result` | tele에 없음, stat만 | ✅ v2.4 일치 | `app.c` tele/pb |
| CMS PB 파싱 | `pb[]` 배열 기준 | ✅ v2.4 | `server.js` — `result` 불필요 |
| DEV ALL_DATA | `{"data":"ALL_DATA"}` | ✅ 일치 | tele 9종 일괄 발행 |
| ONLINE payload | `conn:1` 포함 | ⚠️ `conn` 없음 | `app.c` L379 — 선택적 |
| 브로커 IP 변경 | `set_broker` (v2.0) | ✅ 구현 완료 | 섹션 2B |
| 외부 입력 4채널 | `dev/tele/input` (v2.0) | ✅ 구현 완료 (기존엔 죽은 코드) | 섹션 2A |
| 구 테스트 명령 | `dev/{MAC}/cmnd/…` | ❌ 불일치 | `프로토콜 명령어 모음.txt` 구버전 |

### 11.1 레거시 명령 (구버전 — 사용 금지)

아래는 초기 테스트 메모 형식으로 **현재 MOS 코드와 불일치**:

```
dev/0008DC77631F/cmnd/pb/power     →  dev/cmnd/pb
{"ch":1,"pwr":"ON"}                →  {"b_id":N,"ch":1,"sw":1}   (N=tele 실측 b_id)
{"pwr_all":"11001100"}             →  {"b_id":N,"set_pb":"11001100"}
{"device":"ALL_DATA"}              →  {"data":"ALL_DATA"}
{"device":"RESET"}                 →  {"reset":"DEV_RESET"}
```

---

## 12. 테스트 예시 (mosquitto CLI)

```bash
# 브로커 구독 (전체 tele)
mosquitto_sub -h 192.168.0.100 -p 1883 -t "dev/tele/#" -v

# PB 채널 1 ON (N을 tele/pb의 b_id로 교체)
mosquitto_pub -h 192.168.0.100 -t "dev/cmnd/pb" \
  -m '{"b_id":3,"ch":1,"sw":1}'

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

## 부록 C — v2.0 변경 사항 상세 (2026-08-10)

### C.1 기능 추가
| 항목 | 내용 |
|------|------|
| 외부 입력 4채널 | PD4~PD7 스캔 로직이 **어디서도 호출되지 않던 죽은 코드**였음(입력이 절대 갱신 안 됨) → 센서 태스크에 연결, 상승엣지 카운트 + 변경 시 즉시 발행 추가. 섹션 2A |
| STATIC IP / 브로커 Flash 저장 | `net_config.c` 신규 (내부 Flash), CMS 유지보수 화면에서 변경. 섹션 2B |
| `tele/state`에 `ip`/`mode` 필드 | 현재 실제 접속 IP·모드를 웹 UI에 노출하기 위함 |
| 자동 롤백 | 네트워크/브로커 설정 오입력 시 자동으로 이전 정상값 복구 (섹션 2B.3) |

### C.2 안정성 수정 (재접속 문제)
현장에서 "약 40분마다 MQTT 접속이 끊기고 이후 재접속이 안 됨" 현상이 보고되어 원인을
추적한 결과, 아래 3가지 문제가 확인되어 수정되었습니다.

| # | 원인 | 증거 | 조치 |
|---|------|------|------|
| 1 | `MQTT_SOCKET_NUM`(0)과 `SHELTER_DHCP_SOCKET_NUM`(0)이 **동일한 소켓 번호**. DHCP 리스 갱신(T1 타이머, ~30~40분 주기) 시 라이브러리가 소켓 0을 UDP로 재오픈 → MQTT TCP 소켓 파괴 | 로그의 `SR=0x22`(=SOCK_UDP) | `SHELTER_DHCP_SOCKET_NUM`을 3번으로 분리 (0=MQTT, 1=SNTP, 2=DNS, 3=DHCP) |
| 2 | 소켓 분리 후에도 재현 — DHCP 리스 **갱신**(IP 불변) 때마다 `wizchip_setnetinfo()`를 호출해 W5500 **칩 공통 레지스터**(SHAR/GAR/SUBR/SIPR)를 매번 재작성 → 이미 ESTABLISHED인 MQTT 소켓에 부작용 | `SR=0x00`(SOCK_CLOSED)으로 재현 패턴 변화 | `cb_dhcp_ip_assign()`에서 IP/GW/SN/MAC이 실제로 바뀌었을 때만 재작성하도록 수정 |
| 3 | RS485 통신 4곳(`cwt_th03s.c`, `himpel.c`, `modbus_lg.c`, `power_board.c`)에 **타임아웃 없는 UART TC-flag busy-wait** — 라인 이상 시 센서 태스크가 영구 정지 → 그 이후 모든 report 중단 | "재접속 후 report가 전혀 안 됨" | 4곳 모두 50ms 타임아웃 추가 |

추가로 `mqtt_interface.c`의 `connect()`에 4초 강제 타임아웃(+ `osDelay` yield)을 보조
안전장치로 추가했고, `25LC256.c`(EEPROM 드라이버, v2.0에서 최초로 실사용)의
`EEPROM_SPI_WaitStandbyState()`에도 동일한 이유로 200ms 타임아웃을 추가했습니다.
드롭 감지 시 로그에 `Sn_IR`(DISCON/TIMEOUT 비트) 값도 함께 출력하도록 진단을 강화했습니다.

> 위 표의 #1, #2는 **현장 로그로 근거가 확인된 원인**입니다. 다만 40분 주기 드롭이
> 100% 재현 불가한 상황이었던 만큼, 패치 이후에도 계속 모니터링이 필요합니다.

---

## 부록 D — v2.1 변경 사항 (2026-08-12)

| 항목 | 내용 |
|------|------|
| 직결 프로비저닝 | MQTT 90s 미연결 + 링크 UP → `192.168.0.100:80` HTTP 설정 페이지 (`prov_config.c`) |
| 양산 표준 | 펌웨어 1종 + PC 직결 최초 설정. `config.h`는 Flash empty fallback |
| CMS UI | 네트워크 메뉴 → **유지보수** 역할 (최초 설정은 직결 HTTP) |
| 설치 가이드 | `Shelter_Installation_Guide.md` 신규 |
| Flash 저장 | LAN 모드(DHCP/STATIC) + 브로커 — `NetConfig_SaveAndApplyEx()` |

---

## 부록 E — v2.4 변경 사항 (2026-08-13)

| 항목 | 내용 |
|------|------|
| AP tele | `temp_out`, `tvoc` 필드 추가. mode: CLEAN/VENT/BYPASS/HEATER. spd **0~4** |
| AC spd | LG 풍량 **1~6** (문서·펌웨어 정합) |
| tele/pb | **`result` 제거** — stat/pb만 `result` 포함 |
| 복합 cmnd | AC/AP: 한 JSON에 pwr+mode+temp 등 **다필드 동시 전송** 지원 |
| CMS | PB tele `pb[]` 기준 파싱, `current_b_id` 초기 **0**, 출근 스케줄 `set_ac`/`set_ap` |
| Himpel | 규격·엑셀 표기 **RS485** (Modbus 아님) |

---

## 부록 F — v2.5 변경 사항 (2026-08-13)

| 항목 | 내용 |
|------|------|
| DEV_RESET | **소프트 리부트** (`HAL_NVIC_SystemReset`) — Flash·네트워크 유지. 공장 초기화는 `set_net_reset` |
| PB 펌웨어 | 0xAA 릴레이 순차 간격 **200ms** (기존 2000ms 오류 수정) |
| stat/pb 실패 | PB 온라인·b_id 불일치: `conn:1, result:0` / PB 미연결: `conn:0, result:0` |
| CMS PB | `lastPbDataValid`·`isPbCmdReady()` — tele로 b_id 학습 전 PB cmnd·스케줄 PB 생략 |
| 환경센서 | UP-DM010UB(UART), HC-SD(th_in, I2C), CWT TH03S(th_out, Modbus) — 오프라인 시 동일 스키마 + `conn:0` |
| CMS UI | PB DIP ID 표시, AP `humi`, 팬 RPM, PB 낙관적 UI `{ch,sw}` |

---

## 부록 B — 향후 (TLS / QMEX)

- `TLS_version/` — QMEX TLS 브로커 (`8883`, mbedTLS)
- `쉘터_MQTT_프로토콜_규격서.xlsx` — TLS 버전 상세 규격
- `power_board` 코드는 MOS/TLS **공용**
- TLS 통합 시 브로커·인증만 변경, MQTT JSON 규격은 동일 유지 예정
