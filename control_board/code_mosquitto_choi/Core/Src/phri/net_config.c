/**
 * @file net_config.c
 * @brief net_config.h 구현. 25LC256 EEPROM(SPI6)에 STATIC IP/브로커 설정 저장.
 *        (DHCP 콜백 로직은 건드리지 않음 — w5500_ctrl.c 참고)
 */
#include "net_config.h"
#include "config.h"
#include "25LC256.h"
#include <string.h>
#include <stdio.h>

extern SPI_HandleTypeDef hspi6;

#define NET_CFG_MAGIC      0x53484C33u   /* "SHL3" */

/* EEPROM 레이아웃 (25LC256, 바이트 오프셋) */
#define OFF_MAGIC          0    /* 4 bytes */
#define OFF_CURRENT        8    /* NetRuntimeConfig, sizeof <= 20 bytes */
#define OFF_BACKUP         40
#define OFF_PENDING_FLAG   72
#define OFF_FAIL_COUNT     73
#define OFF_CHECKSUM       74   /* 2 bytes */

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
    const uint8_t ip[4]  = SHELTER_NET_IP;
    const uint8_t sn[4]  = SHELTER_NET_SUBNET;
    const uint8_t gw[4]  = SHELTER_NET_GATEWAY;
    const uint8_t dns[4] = SHELTER_NET_DNS;
    const uint8_t bip[4] = SHELTER_MQTT_BROKER_IP;

    memset(c, 0, sizeof(*c));
    memcpy(c->ip,  ip,  4);
    memcpy(c->sn,  sn,  4);
    memcpy(c->gw,  gw,  4);
    memcpy(c->dns, dns, 4);
    memcpy(c->broker_ip, bip, 4);
    c->broker_port = SHELTER_MQTT_BROKER_PORT;
}

static bool eeprom_read(uint16_t off, void *buf, uint16_t len)
{
    return EEPROM_SPI_ReadBuffer((uint8_t *)buf,
            SHELTER_NET_EEPROM_BASE_ADDR + off, len) == EEPROM_STATUS_COMPLETE;
}

static bool eeprom_write(uint16_t off, const void *buf, uint16_t len)
{
    return EEPROM_SPI_WriteBuffer((uint8_t *)buf,
            SHELTER_NET_EEPROM_BASE_ADDR + off, len) == EEPROM_STATUS_COMPLETE;
}

static void write_current(const NetRuntimeConfig *c)
{
    uint16_t sum = calc_checksum(c);
    eeprom_write(OFF_CURRENT, c, sizeof(*c));
    eeprom_write(OFF_CHECKSUM, &sum, sizeof(sum));
}

static bool read_current(NetRuntimeConfig *out)
{
    NetRuntimeConfig tmp;
    uint16_t sum_stored = 0;

    if (!eeprom_read(OFF_CURRENT, &tmp, sizeof(tmp))) return false;
    if (!eeprom_read(OFF_CHECKSUM, &sum_stored, sizeof(sum_stored))) return false;
    if (calc_checksum(&tmp) != sum_stored) return false;

    *out = tmp;
    return true;
}

void NetConfig_CheckRollback(void)
{
    uint32_t magic = 0;
    uint8_t  pending = 0, fail_cnt = 0;

    EEPROM_SPI_INIT(&hspi6);

    if (!eeprom_read(OFF_MAGIC, &magic, sizeof(magic)) || magic != NET_CFG_MAGIC) {
        return; /* EEPROM 미기록(최초) 또는 EEPROM 미응답 — 롤백 대상 없음 */
    }

    eeprom_read(OFF_PENDING_FLAG, &pending, sizeof(pending));
    if (pending == 0) return;

    eeprom_read(OFF_FAIL_COUNT, &fail_cnt, sizeof(fail_cnt));
    fail_cnt++;
    printf("[NETCFG] Pending config, boot attempt %u/%u\r\n",
           fail_cnt, (unsigned)SHELTER_NET_ROLLBACK_MAX_FAILS);

    if (fail_cnt >= SHELTER_NET_ROLLBACK_MAX_FAILS) {
        NetRuntimeConfig backup;
        if (eeprom_read(OFF_BACKUP, &backup, sizeof(backup))) {
            printf("[NETCFG] Rollback triggered! Restoring previous known-good config.\r\n");
            write_current(&backup);
            pending = 0; fail_cnt = 0;
            eeprom_write(OFF_PENDING_FLAG, &pending, sizeof(pending));
            eeprom_write(OFF_FAIL_COUNT, &fail_cnt, sizeof(fail_cnt));
            return;
        }
    }
    eeprom_write(OFF_FAIL_COUNT, &fail_cnt, sizeof(fail_cnt));
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
        eeprom_read(OFF_PENDING_FLAG, &pending_flag, sizeof(pending_flag));
        g_net_cfg_pending = (pending_flag != 0);
        printf("[NETCFG] Loaded from EEPROM (broker=%d.%d.%d.%d:%u, pending=%d)\r\n",
               g_net_cfg.broker_ip[0], g_net_cfg.broker_ip[1],
               g_net_cfg.broker_ip[2], g_net_cfg.broker_ip[3], g_net_cfg.broker_port,
               g_net_cfg_pending);
        return true;
    }

    /* EEPROM 최초 출하 또는 응답 없음 — config.h 기본값 사용 (안전 폴백) */
    load_defaults(&g_net_cfg);
    g_net_cfg_pending = false;

    /* EEPROM이 실제로 응답했을 때만 기본값을 기록 시도 (미배선/이상 시 무시하고 계속 진행) */
    magic = NET_CFG_MAGIC;
    uint8_t pending = 0, fail_cnt = 0;
    if (eeprom_write(OFF_MAGIC, &magic, sizeof(magic))) {
        write_current(&g_net_cfg);
        eeprom_write(OFF_BACKUP, &g_net_cfg, sizeof(g_net_cfg));
        eeprom_write(OFF_PENDING_FLAG, &pending, sizeof(pending));
        eeprom_write(OFF_FAIL_COUNT, &fail_cnt, sizeof(fail_cnt));
        printf("[NETCFG] EEPROM empty. Wrote compile-time defaults from config.h\r\n");
    } else {
        printf("[NETCFG] EEPROM not responding — using config.h compile-time defaults only (not persisted)\r\n");
    }
    return false;
}

void NetConfig_SaveAndApply(const NetRuntimeConfig *new_cfg)
{
    NetRuntimeConfig backup_slot;
    uint8_t pending = 1, fail_cnt = 0;

    if (read_current(&backup_slot)) {
        eeprom_write(OFF_BACKUP, &backup_slot, sizeof(backup_slot));
    }

    write_current(new_cfg);
    eeprom_write(OFF_PENDING_FLAG, &pending, sizeof(pending));
    eeprom_write(OFF_FAIL_COUNT, &fail_cnt, sizeof(fail_cnt));
    g_net_cfg_pending = true;

    printf("[NETCFG] New config saved (pending). Rebooting to apply...\r\n");
}

void NetConfig_ConfirmBoot(void)
{
    uint8_t pending = 0;
    NetRuntimeConfig cur;

    if (!g_net_cfg_pending) return;
    if (!read_current(&cur)) return;

    eeprom_write(OFF_BACKUP, &cur, sizeof(cur));
    eeprom_write(OFF_PENDING_FLAG, &pending, sizeof(pending));
    g_net_cfg_pending = false;
    printf("[NETCFG] Config confirmed (MQTT connected OK). Rollback timer cleared.\r\n");
}
