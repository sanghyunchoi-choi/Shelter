/**
 * @file net_config.h
 * @brief STM32H7 내장 플래시 기반 네트워크/브로커 설정 프로파일 영구 저장소 헤더
 */
#ifndef INC_NET_CONFIG_H_
#define INC_NET_CONFIG_H_

#include "main.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t  net_mode;     /* 0 = STATIC 고정 IP, 1 = DHCP 자동 할당 */
    uint8_t  ip[4];        /* 제어 보드 IP 주소 세트 */
    uint8_t  sn[4];        /* 서브넷 마스크 세트 */
    uint8_t  gw[4];        /* 게이트웨이 주소 세트 */
    uint8_t  dns[4];       /* DNS 주소 세트 */
    uint8_t  broker_ip[4]; /* 원격 MQTT 브로커 PC IP 세트 */
    uint16_t broker_port;  /* MQTT 브로커 포트 번호 */
    uint16_t checksum;     /* 플래시 데이터 무결성 검증용 체크섬 가드 */
} __attribute__((packed)) NetRuntimeConfig;

extern NetRuntimeConfig g_net_cfg;
extern volatile bool g_net_cfg_pending;

typedef enum {
	NET_SAVE_PROFILE = 0,      /* Flash 저장 후 재부팅 (STATIC/DHCP+브로커) */
	NET_SAVE_FACTORY_DHCP = 1  /* Flash erase → DHCP 공장 초기화 */
} NetConfigSaveAction;

void NetConfig_CheckRollback(void);
bool NetConfig_Load(void);
bool NetConfig_HasFlashProfile(void);
bool NetConfig_SaveAndApply(const NetRuntimeConfig *new_cfg);
bool NetConfig_SaveAndApplyEx(const NetRuntimeConfig *new_cfg, NetConfigSaveAction action);
void NetConfig_ConfirmBoot(void);
void NetConfig_ExecuteFlashEraseAndReboot(void);

#endif /* INC_NET_CONFIG_H_ */
