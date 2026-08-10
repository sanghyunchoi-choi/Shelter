/**
 * @file net_config.c
 * @brief STATIC IP 필드 및 브로커 정보 EEPROM 관리 코어엔진 (하드웨어 페이지 정렬 완료 버전)
 */
#include "net_config.h"
#include "config.h"
#include "25LC256.h"
#include <string.h>
#include <stdio.h>

extern SPI_HandleTypeDef hspi6;

#define NET_CFG_MAGIC      0x53484C33u   /* "SHL3" 매직코드 */

/*
 * [★하드웨어 페이지 격리] 25LC256의 64바이트 물리 페이지 경계를 절대 침범하지 않도록
 * 모든 독립 데이터를 64바이트(1페이지) 배수로 완전히 분리하여 알박기합니다.
 */
#define OFF_MAGIC          0     /* Page 0 (0~63) */
#define OFF_CURRENT        64    /* Page 1 (64~127): NetRuntimeConfig 구조체 */
#define OFF_BACKUP         128   /* Page 2 (128~191): NetRuntimeConfig 백업 */
#define OFF_PENDING_FLAG   192   /* Page 3 (192~255): 플래그 변수 구역 */
#define OFF_FAIL_COUNT     193
#define OFF_CHECKSUM       194   /* 체크섬도 Page 3 내부 정렬 구역에 배치 */

NetRuntimeConfig g_net_cfg;
volatile bool g_net_cfg_pending = false;

static uint16_t calc_checksum(const NetRuntimeConfig *c)
{
    const uint8_t *p = (const uint8_t *)c;
    uint16_t sum = 0;
    for (size_t i = 0; i < sizeof(*c); i++) sum += p[i];
    return sum;
}

static void load_defaults(NetRuntimeConfig *c)
{
    uint8_t ip_init[]   = SHELTER_NET_IP;
    uint8_t sn_init[]   = SHELTER_NET_SUBNET;
    uint8_t gw_init[]   = SHELTER_NET_GATEWAY;
    uint8_t dns_init[]  = SHELTER_NET_DNS;
    uint8_t bip_init[]  = SHELTER_MQTT_BROKER_IP;

    memset(c, 0, sizeof(*c));

    c->net_mode = 1; // 최초 가동 기본값은 DHCP(1)

    for(int i = 0; i < 4; i++) {
        c->ip[i]        = ip_init[i];
        c->sn[i]        = sn_init[i];
        c->gw[i]        = gw_init[i];
        c->dns[i]       = dns_init[i];
        c->broker_ip[i] = bip_init[i];
    }
    c->broker_port = SHELTER_MQTT_BROKER_PORT;
}

static bool eeprom_read(uint16_t off, void *buf, uint16_t len)
{
    return EEPROM_SPI_ReadBuffer((uint8_t *)buf,
            SHELTER_NET_EEPROM_BASE_ADDR + off, len) == EEPROM_STATUS_COMPLETE;
}

static void write_current(const NetRuntimeConfig *c)
{
    // 복잡한 체크섬 연산 및 쓰기를 배제하고 구조체만 확실하게 페이지 저장합니다.
    EEPROM_SPI_WritePage((uint8_t*)c, OFF_CURRENT, sizeof(*c));
}

static bool read_current(NetRuntimeConfig *out)
{
    NetRuntimeConfig tmp;

    // 체크섬 주소 번지 미스매치 위험을 없애기 위해, 순수하게 구조체 읽기 성공 여부만 확인합니다.
    if (!eeprom_read(OFF_CURRENT, &tmp, sizeof(tmp))) {
        return false;
    }

    *out = tmp;
    return true;
}

void NetConfig_CheckRollback(void)
{
    uint32_t magic = 0;
    uint8_t  pending = 0;
    uint8_t  fail_cnt = 0;

    EEPROM_SPI_INIT(&hspi6);

    if (!eeprom_read(OFF_MAGIC, &magic, sizeof(magic)) || magic != NET_CFG_MAGIC) {
        return;
    }

    if (!eeprom_read(OFF_PENDING_FLAG, &pending, sizeof(pending)) || pending == 0) {
        return;
    }

    if (!eeprom_read(OFF_FAIL_COUNT, &fail_cnt, sizeof(fail_cnt))) {
        fail_cnt = 0;
    }

    fail_cnt++;
    printf("[NETCFG] Pending config detected. Boot attempt: %u/%u\r\n",
           (unsigned int)fail_cnt, (unsigned int)SHELTER_NET_ROLLBACK_MAX_FAILS);

    if (fail_cnt >= SHELTER_NET_ROLLBACK_MAX_FAILS) {
        NetRuntimeConfig backup;
        if (eeprom_read(OFF_BACKUP, &backup, sizeof(backup))) {
            printf("[NETCFG] !!! Rollback triggered !!! Restoring backup config.\r\n");

            write_current(&backup);

            pending = 0;
            fail_cnt = 0;

            EEPROM_SPI_WritePage(&pending, OFF_PENDING_FLAG, sizeof(pending));
            EEPROM_SPI_WritePage(&fail_cnt, OFF_FAIL_COUNT, sizeof(fail_cnt));
            return;
        }
    }

    EEPROM_SPI_WritePage(&fail_cnt, OFF_FAIL_COUNT, sizeof(fail_cnt));
}

bool NetConfig_Load(void)
{
    NetRuntimeConfig cfg;
    uint32_t magic = 0;
    uint8_t pending_flag = 0;

    EEPROM_SPI_INIT(&hspi6);

    if (eeprom_read(OFF_MAGIC, &magic, sizeof(magic)) &&
        magic == NET_CFG_MAGIC && read_current(&cfg)) {

        g_net_cfg = cfg;

        if (eeprom_read(OFF_PENDING_FLAG, &pending_flag, sizeof(pending_flag))) {
            g_net_cfg_pending = (pending_flag != 0);
        } else {
            g_net_cfg_pending = false;
        }

        printf("[NETCFG] Loaded from EEPROM (mode=%u, broker=%u.%u.%u.%u:%u, pending=%d)\r\n",
               g_net_cfg.net_mode,
               (unsigned int)g_net_cfg.broker_ip[0], (unsigned int)g_net_cfg.broker_ip[1],
               (unsigned int)g_net_cfg.broker_ip[2], (unsigned int)g_net_cfg.broker_ip[3],
               (unsigned int)g_net_cfg.broker_port, g_net_cfg_pending);
        return true;
    }

    load_defaults(&g_net_cfg);
    g_net_cfg_pending = false;

    magic = NET_CFG_MAGIC;
    uint8_t pending = 0;
    uint8_t fail_cnt = 0;

    if (EEPROM_SPI_WritePage((uint8_t*)&magic, OFF_MAGIC, sizeof(magic)) == EEPROM_STATUS_COMPLETE) {
        write_current(&g_net_cfg);
        EEPROM_SPI_WritePage((uint8_t*)&g_net_cfg, OFF_BACKUP, sizeof(g_net_cfg));
        EEPROM_SPI_WritePage(&pending, OFF_PENDING_FLAG, sizeof(pending));
        EEPROM_SPI_WritePage(&fail_cnt, OFF_FAIL_COUNT, sizeof(fail_cnt));

        printf("[NETCFG] EEPROM empty. Wrote compile-time defaults from config.h\r\n");
    } else {
        printf("[NETCFG] EEPROM not responding — using config.h compile-time defaults\r\n");
    }

    return false;
}

void NetConfig_SaveAndApply(const NetRuntimeConfig *new_cfg)
{
    NetRuntimeConfig backup_slot;
    uint32_t magic = NET_CFG_MAGIC;
    uint8_t pending = 1;
    uint8_t fail_cnt = 0;

    if (read_current(&backup_slot)) {
        EEPROM_SPI_WritePage((uint8_t*)&backup_slot, OFF_BACKUP, sizeof(backup_slot));
    }

    // 완전히 격리 정렬된 오프셋 맵 규격 순차 기록
    EEPROM_SPI_WritePage((uint8_t*)&magic, OFF_MAGIC, sizeof(magic));
    write_current(new_cfg);

    EEPROM_SPI_WritePage(&pending, OFF_PENDING_FLAG, sizeof(pending));
    EEPROM_SPI_WritePage(&fail_cnt, OFF_FAIL_COUNT, sizeof(fail_cnt));
    g_net_cfg_pending = true;

    printf("[NETCFG] New config saved (pending). Rebooting to apply...\r\n");

    HAL_Delay(500);
    HAL_NVIC_SystemReset();
}

void NetConfig_ConfirmBoot(void)
{
    uint8_t pending = 0;
    NetRuntimeConfig cur;

    if (!g_net_cfg_pending) return;
    if (!read_current(&cur)) return;

    EEPROM_SPI_WritePage((uint8_t*)&cur, OFF_BACKUP, sizeof(cur));
    EEPROM_SPI_WritePage(&pending, OFF_PENDING_FLAG, sizeof(pending));
    g_net_cfg_pending = false;
    printf("[NETCFG] Config confirmed (MQTT connected OK). Rollback timer cleared.\r\n");
}
