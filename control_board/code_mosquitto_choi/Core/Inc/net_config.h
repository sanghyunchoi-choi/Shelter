/**
 * @file net_config.h
 * @brief STATIC IP 필드 / MQTT 브로커 IP·포트의 EEPROM(25LC256) 런타임 저장.
 *
 * ★ 설계 원칙(2026-08-10): 지금 막 안정화된 DHCP 콜백 로직(w5500_ctrl.c의
 * cb_dhcp_ip_assign 등)은 절대 건드리지 않습니다. STATIC/DHCP "모드" 선택은
 * 여전히 config.h의 컴파일타임 매크로(SHELTER_NET_USE_STATIC/DHCP)로 결정됩니다.
 * 이 모듈이 EEPROM에서 런타임으로 바꿔주는 것은 다음 두 가지뿐입니다:
 *   1) STATIC 모드일 때 사용할 IP/서브넷/게이트웨이/DNS 값
 *   2) MQTT 브로커 IP/포트 (STATIC/DHCP 어느 모드든 공통으로 사용)
 *
 * 웹서버 "네트워크 설정" 화면에서 값을 바꾸면 dev/cmnd/dev 로
 * {"set_net":{...}} / {"set_broker":{...}} 명령이 오고, 보드는 EEPROM에
 * 저장 후 재부팅하여 적용합니다. 안전을 위해 이전 값을 backup으로 보관하고,
 * 새 값으로 SHELTER_NET_ROLLBACK_TIMEOUT_MS 안에 MQTT 연결에 성공하지
 * 못하면 자동으로 이전 값으로 되돌립니다.
 *
 * 주의: 이 모듈은 25LC256 SPI EEPROM(SPI6)을 처음으로 실제 사용합니다.
 * 기존 코드베이스에는 이 드라이버를 호출하는 곳이 전혀 없었으므로,
 * 현장 보드에서 최초 1회는 EEPROM 읽기/쓰기 동작을 직접 확인해 주세요.
 * (혹시 EEPROM이 배선되어 있지 않거나 응답하지 않으면 NetConfig_Load()가
 * false를 반환하고 config.h의 컴파일타임 기본값으로 자동 대체되므로,
 * 안전하게 실패합니다 — 보드가 멈추지는 않습니다.)
 */
#ifndef INC_NET_CONFIG_H_
#define INC_NET_CONFIG_H_

#include "main.h"
#include <stdbool.h>
#include <stdint.h>

/* net_config.h 구조체 수정본 */
typedef struct {
    uint8_t  net_mode;     // 0 = STATIC, 1 = DHCP
    uint8_t  ip[4];
    uint8_t  sn[4];
    uint8_t  gw[4];
    uint8_t  dns[4];
    uint8_t  broker_ip[4];
    uint16_t broker_port;
} __attribute__((packed)) NetRuntimeConfig; // ★ [필수] 1바이트 단위 패킹 지정을 통해 패딩 오염 원천 차단


extern NetRuntimeConfig g_net_cfg;

/* true = 아직 "확정"되지 않은 새 설정으로 부팅됨(직전에 변경됨).
   app.c는 이 값이 true인 동안 일정 시간 내 MQTT 연결에 실패하면
   재부팅해서 NetConfig_CheckRollback()의 실패 카운터가 누적되게 합니다. */
extern volatile bool g_net_cfg_pending;

/** 부팅 시 W5500_Init() 이전에 1회 호출. EEPROM에 유효한 값이 있으면 로드,
 *  없거나(최초 출하) 읽기에 실패하면 config.h 컴파일타임 값으로 자동 대체. */
bool NetConfig_Load(void);

/** NetConfig_Load()보다 먼저 호출. 직전 부팅이 "미확정" 상태로 반복
 *  재부팅되었으면 SHELTER_NET_ROLLBACK_MAX_FAILS회를 넘길 때 backup으로 복구. */
void NetConfig_CheckRollback(void);

/** 웹서버/MQTT로부터 새 설정을 받았을 때 호출. 현재 값을 backup으로 옮기고
 *  새 값을 "미확정" 상태로 저장. 호출 후 재부팅해야 적용됨. */
void NetConfig_SaveAndApply(const NetRuntimeConfig *new_cfg);

/** MQTT 최초 연결 성공 시 1회 호출. pending 상태였다면 확정(commit) 처리. */
void NetConfig_ConfirmBoot(void);

#endif /* INC_NET_CONFIG_H_ */
