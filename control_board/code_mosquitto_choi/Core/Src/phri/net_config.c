/**
 * @file net_config.c
 * @brief 제어보드 IP & MQTT 브로커 설정 STM32H7 내장 플래시 저장/복구 코어 매니저
 */
#include "net_config.h"
#include "config.h"
#include <string.h>
#include <stdio.h>

#define NET_CFG_MAGIC           0x53484C33u   /* "SHL3" 매직코드 */

/* STM32H743ZIT6 펌웨어 충돌 없는 안전 구역 (Bank 2, 마지막 Sector 7) 고정 */
#define FLASH_USER_START_ADDR   0x081E0000u
#define FLASH_SECTOR_NUM        7
#define FLASH_BANK_NUM          FLASH_BANK_2

/* 하드웨어 32바이트 정렬 배치 맵 정의 (데이터 꼬임 전면 차단) */
#define OFF_MAGIC               0
#define OFF_CURRENT             32
#define OFF_BACKUP              64
#define OFF_PENDING_FLAG        96
#define OFF_FAIL_COUNT          128

NetRuntimeConfig g_net_cfg;
volatile bool g_net_cfg_pending = false;

/* 내장 플래시 섹터 완전 초기화(지우기) */
/* 내장 플래시 섹터 완전 초기화 (STM32H7 전용 Ex_Erase 규격 정렬) */
static bool flash_erase_sector(void)
{
    FLASH_EraseInitTypeDef EraseInitStruct;
    uint32_t SectorError = 0;

    EraseInitStruct.TypeErase    = FLASH_TYPEERASE_SECTORS;
    EraseInitStruct.Banks        = FLASH_BANK_NUM;
    EraseInitStruct.Sector       = FLASH_SECTOR_NUM;
    EraseInitStruct.NbSectors    = 1;
    EraseInitStruct.VoltageRange = FLASH_VOLTAGE_RANGE_3; // 3.3V 보드 전위 레벨

    if (HAL_FLASH_Unlock() != HAL_OK) return false;

    // H7 플래시 컨트롤러 캐시 에러 플래그 전면 비우기
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS_BANK2);

    // ★ [링커 에러 해결 픽스] H7 정식 명칭인 HAL_FLASHEx_Erase API로 바인딩 변경!!
    if (HAL_FLASHEx_Erase(&EraseInitStruct, &SectorError) != HAL_OK) {
        HAL_FLASH_Lock();
        return false;
    }

    HAL_FLASH_Lock();
    return true;
}


/* 플래시 영역 램 버퍼 스트리밍 복사 */
static void flash_read_bytes(uint32_t offset, void *buf, uint32_t len)
{
    uint32_t src = FLASH_USER_START_ADDR + offset;
    memcpy(buf, (const void*)src, len);
}

/* STM32H7 256비트(32바이트) 단위 안전 덮어쓰기 라이팅 엔진 */
static bool flash_write_bytes(uint32_t offset, const void *buf, uint32_t len)
{
    uint8_t page_buf[256];

    // 1. 현재 섹터에 들어있는 상태 전체를 안전하게 램 버퍼로 복사
    flash_read_bytes(0, page_buf, 256);

    // 2. 수정을 원하는 주소 오프셋 구역만 램 상에서 정밀 갱신
    memcpy(&page_buf[offset], buf, len);

    // 3. 플래시 물리 특성 반영을 위해 타겟 섹터 깨끗이 밀어내기
    if (!flash_erase_sector()) return false;

    if (HAL_FLASH_Unlock() != HAL_OK) return false;
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS_BANK2);

    // 4. 32바이트(Flash Word) 배수로 연속 내장 각인 실행
    uint32_t dest_addr = FLASH_USER_START_ADDR;
    uint32_t src_addr  = (uint32_t)page_buf;

    for (int i = 0; i < 256; i += 32) {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD, dest_addr + i, src_addr + i) != HAL_OK) {
            HAL_FLASH_Lock();
            return false;
        }
    }

    HAL_FLASH_Lock();
    return true;
}

static void load_defaults(NetRuntimeConfig *c)
{
    uint8_t ip_init[]   = SHELTER_NET_IP;
    uint8_t sn_init[]   = SHELTER_NET_SUBNET;
    uint8_t gw_init[]   = SHELTER_NET_GATEWAY;
    uint8_t dns_init[]  = SHELTER_NET_DNS;
    uint8_t bip_init[]  = SHELTER_MQTT_BROKER_IP;

    memset(c, 0, sizeof(*c));

    c->net_mode = 1; /* 최초 공장 출하 시 기본값은 무조건 1 (DHCP 모드 기동) */

    for(int i = 0; i < 4; i++) {
        c->ip[i]        = ip_init[i];
        c->sn[i]        = sn_init[i];
        c->gw[i]        = gw_init[i];
        c->dns[i]       = dns_init[i];
        c->broker_ip[i] = bip_init[i];
    }
    c->broker_port = SHELTER_MQTT_BROKER_PORT;
}

static void write_current(const NetRuntimeConfig *c)
{
    flash_write_bytes(OFF_CURRENT, c, sizeof(*c));
}

static bool read_current(NetRuntimeConfig *out)
{
    flash_read_bytes(OFF_CURRENT, out, sizeof(*out));
    return true;
}

void NetConfig_CheckRollback(void)
{
    uint32_t magic = 0;
    uint8_t  pending = 0;
    uint8_t  fail_cnt = 0;

    flash_read_bytes(OFF_MAGIC, &magic, sizeof(magic));
    if (magic != NET_CFG_MAGIC) return;

    flash_read_bytes(OFF_PENDING_FLAG, &pending, sizeof(pending));
    if (pending == 0) return;

    flash_read_bytes(OFF_FAIL_COUNT, &fail_cnt, sizeof(fail_cnt));
    fail_cnt++;
    printf("[NETCFG] Pending config detected. Boot attempt: %u/%u\r\n",
           (unsigned int)fail_cnt, (unsigned int)SHELTER_NET_ROLLBACK_MAX_FAILS);

    if (fail_cnt >= SHELTER_NET_ROLLBACK_MAX_FAILS) {
        NetRuntimeConfig backup;
        flash_read_bytes(OFF_BACKUP, &backup, sizeof(backup));
        printf("[NETCFG] !!! Rollback triggered !!! Restoring backup config.\r\n");

        write_current(&backup);

        pending = 0;
        fail_cnt = 0;

        flash_write_bytes(OFF_PENDING_FLAG, &pending, sizeof(pending));
        flash_write_bytes(OFF_FAIL_COUNT, &fail_cnt, sizeof(fail_cnt));
        return;
    }

    flash_write_bytes(OFF_FAIL_COUNT, &fail_cnt, sizeof(fail_cnt));
}

bool NetConfig_Load(void)
{
    NetRuntimeConfig cfg;
    uint32_t magic = 0;
    uint8_t pending_flag = 0;

    flash_read_bytes(OFF_MAGIC, &magic, sizeof(magic));

    if (magic == NET_CFG_MAGIC) {
        read_current(&cfg);
        g_net_cfg = cfg;

        flash_read_bytes(OFF_PENDING_FLAG, &pending_flag, sizeof(pending_flag));
        g_net_cfg_pending = (pending_flag != 0);

        // 보드 고정 IP 주소 및 연동 브로커 PC 정보 통합 로드 출력 보완 완료
        printf("[NETCFG] Loaded from Internal Flash (mode=%u, board_ip=%u.%u.%u.%u, broker=%u.%u.%u.%u:%u, pending=%d)\r\n",
               g_net_cfg.net_mode,
               g_net_cfg.ip[0], g_net_cfg.ip[1], g_net_cfg.ip[2], g_net_cfg.ip[3],
               g_net_cfg.broker_ip[0], g_net_cfg.broker_ip[1], g_net_cfg.broker_ip[2], g_net_cfg.broker_ip[3],
               g_net_cfg.broker_port, g_net_cfg_pending);
        return true;
    }

    /* 내장 플래시 최초 각인 포매팅 (config.h 베이스라인 이식) */
    load_defaults(&g_net_cfg);
    g_net_cfg_pending = false;

    magic = NET_CFG_MAGIC;
    uint8_t pending = 0;
    uint8_t fail_cnt = 0;

    uint8_t init_block[256];
    memset(init_block, 0xFF, sizeof(init_block));

    memcpy(&init_block[OFF_MAGIC], &magic, sizeof(magic));
    memcpy(&init_block[OFF_CURRENT], &g_net_cfg, sizeof(g_net_cfg));
    memcpy(&init_block[OFF_BACKUP], &g_net_cfg, sizeof(g_net_cfg));
    memcpy(&init_block[OFF_PENDING_FLAG], &pending, sizeof(pending));
    memcpy(&init_block[OFF_FAIL_COUNT], &fail_cnt, sizeof(fail_cnt));

    flash_write_bytes(0, init_block, 256);
    printf("[NETCFG] Internal Flash Empty. Wrote compile-time defaults to Sector 7.\r\n");

    return false;
}

void NetConfig_SaveAndApply(const NetRuntimeConfig *new_cfg)
{
    NetRuntimeConfig backup_slot;
    uint32_t magic = NET_CFG_MAGIC;
    uint8_t pending = 1;
    uint8_t fail_cnt = 0;

    read_current(&backup_slot);

    uint8_t save_block[256];
    memset(save_block, 0xFF, sizeof(save_block));

    memcpy(&save_block[OFF_MAGIC], &magic, sizeof(magic));
    memcpy(&save_block[OFF_CURRENT], new_cfg, sizeof(*new_cfg));
    memcpy(&save_block[OFF_BACKUP], &backup_slot, sizeof(backup_slot));
    memcpy(&save_block[OFF_PENDING_FLAG], &pending, sizeof(pending));
    memcpy(&save_block[OFF_FAIL_COUNT], &fail_cnt, sizeof(fail_cnt));

    flash_write_bytes(0, save_block, 256);
    g_net_cfg_pending = true;

    printf("[NETCFG] New config saved to internal flash. Rebooting to apply...\r\n");

    HAL_Delay(500);
    HAL_NVIC_SystemReset(); /* 저장 즉시 하드웨어 자동 재부팅 */
}

void NetConfig_ConfirmBoot(void)
{
    uint8_t pending = 0;
    NetRuntimeConfig cur;

    if (!g_net_cfg_pending) return;
    read_current(&cur);

    flash_write_bytes(OFF_BACKUP, &cur, sizeof(cur));
    flash_write_bytes(OFF_PENDING_FLAG, &pending, sizeof(pending));
    g_net_cfg_pending = false;
    printf("[NETCFG] Config confirmed (MQTT connected OK). Internal Flash backup synced.\r\n");
}
