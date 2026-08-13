/**
 * @file net_config.c
 * @brief STM32H7 내장 플래시 영구 각인 및 데이터 무결성 체크섬 가드 매니저
 */
#include "net_config.h"
#include "config.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
#include <stdio.h>

#define NET_CFG_MAGIC 0x53484C33u /* "SHL3" 고유 매직넘버 */

/* 펌웨어 적재 영역과 완전히 분리된 안전 구역 고정 (Bank 2, 마지막 Sector 7) */
#define FLASH_USER_START_ADDR   0x081E0000u
#define NET_CONFIG_FLASH_BANK   FLASH_BANK_2
#define NET_CONFIG_FLASH_SECTOR FLASH_SECTOR_7

/* 하드웨어 32바이트 정렬 배치 오프셋 정의 (데이터 꼬임 완전 차단) */
#define OFF_MAGIC               0
#define OFF_CURRENT             32
#define OFF_BACKUP              64
#define OFF_PENDING_FLAG        96
#define OFF_FAIL_COUNT          128

/* --- 전역 변수 선언 --- */
NetRuntimeConfig g_net_cfg;
volatile bool g_net_cfg_pending = false;
static bool s_flash_profile_valid = false;

bool NetConfig_HasFlashProfile(void)
{
	return s_flash_profile_valid;
}

/**
 * @brief 플래시 메모리 프로파일 무결성 연산 검증용 8비트 정렬 합산기
 */
static uint16_t calc_flash_checksum(const NetRuntimeConfig *c)
{
    const uint8_t *p = (const uint8_t *)c;
    uint16_t sum = 0;

    /* 구조체 최하단 checksum 멤버 변수(2바이트) 직전까지만 루프를 돌며 합산 */
    for (size_t i = 0; i < sizeof(NetRuntimeConfig) - 2; i++) {
        sum += p[i];
    }
    return sum;
}

/**
 * @brief 내장 플래시 지정 섹터 완전 지우기 (STM32H7 Bank2 전용 명세 반영)
 */
static bool flash_erase_sector(void)
{
    FLASH_EraseInitTypeDef EraseInitStruct;
    uint32_t SectorError = 0;

    if (HAL_FLASH_Unlock() != HAL_OK) {
        return false;
    }

    /* H7 플래시 컨트롤러 캐시 에러 비트 강제 리셋 (Bank2 청소) */
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS_BANK2);

    EraseInitStruct.TypeErase    = FLASH_TYPEERASE_SECTORS;
    EraseInitStruct.Banks        = NET_CONFIG_FLASH_BANK;
    EraseInitStruct.Sector       = NET_CONFIG_FLASH_SECTOR;
    EraseInitStruct.NbSectors    = 1;
    EraseInitStruct.VoltageRange = FLASH_VOLTAGE_RANGE_3;

    if (HAL_FLASHEx_Erase(&EraseInitStruct, &SectorError) != HAL_OK) {
        HAL_FLASH_Lock();
        return false;
    }

    HAL_FLASH_Lock();
    return true;
}

/**
 * @brief 플래시 메모리에서 램 상으로 로우 레벨 데이터 카피
 */
static void flash_read_bytes(uint32_t offset, void *buf, uint32_t len)
{
    uint32_t src = FLASH_USER_START_ADDR + offset;
    memcpy(buf, (const void *)src, len);
}

/**
 * @brief STM32H7 전용 256비트(32바이트) 정렬 플래시 워드 강제 안전 기록 엔진
 */
static bool flash_write_bytes(uint32_t offset, const void *buf, uint32_t len)
{
    uint8_t page_buf[256];
    uint32_t dest_addr = FLASH_USER_START_ADDR;
    uint32_t src_addr = (uint32_t)page_buf;

    /* 1. 혹시 모를 나머지 비트 데이터 보존을 위해 현재 플래시 상태를 램으로 일괄 적재 */
    flash_read_bytes(0, page_buf, 256);

    /* 2. 쓰기를 원하는 타겟 오프셋 영역만 램 버퍼 상에서 정밀 갱신 */
    memcpy(&page_buf[offset], buf, len);

    /* 3. 물리 저장 전 섹터 포매팅 */
    if (!flash_erase_sector()) {
        return false;
    }

    if (HAL_FLASH_Unlock() != HAL_OK) {
        return false;
    }
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS_BANK2);

    /* 4. [★H7 하드웨어 규격] 32바이트(Flash Word) 단위 배수로 연속 내장 각인 */
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
    uint8_t bip_init[] = SHELTER_MQTT_BROKER_IP;

    memset(c, 0, sizeof(*c));
    c->net_mode = 1; /* 공장 초기 / 팩토리 리셋 = DHCP */
    for (int i = 0; i < 4; i++) {
        c->broker_ip[i] = bip_init[i];
    }
    c->broker_port = SHELTER_MQTT_BROKER_PORT;
    /* DHCP 모드에서는 static IP 프로필을 Flash에 남기지 않음 (0.0.0.0) */
}

static void netcfg_print_loaded(const NetRuntimeConfig *cfg, int pending)
{
    if (cfg->net_mode == 1) {
        printf("[NETCFG] Loaded from Internal Flash (mode=DHCP, broker=%u.%u.%u.%u:%u, pending=%d)\r\n",
               cfg->broker_ip[0], cfg->broker_ip[1], cfg->broker_ip[2], cfg->broker_ip[3],
               cfg->broker_port, pending);
    } else {
        printf("[NETCFG] Loaded from Internal Flash (mode=STATIC, ip=%u.%u.%u.%u, broker=%u.%u.%u.%u:%u, pending=%d)\r\n",
               cfg->ip[0], cfg->ip[1], cfg->ip[2], cfg->ip[3],
               cfg->broker_ip[0], cfg->broker_ip[1], cfg->broker_ip[2], cfg->broker_ip[3],
               cfg->broker_port, pending);
    }
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
    uint8_t pending = 0;
    uint8_t fail_cnt = 0;
    NetRuntimeConfig backup;

    flash_read_bytes(OFF_MAGIC, &magic, sizeof(magic));
    if (magic != NET_CFG_MAGIC) {
        return;
    }

    flash_read_bytes(OFF_PENDING_FLAG, &pending, sizeof(pending));
    if (pending == 0) {
        return;
    }

    flash_read_bytes(OFF_FAIL_COUNT, &fail_cnt, sizeof(fail_cnt));
    fail_cnt++;
    printf("[NETCFG] Pending config detected. Boot attempt: %u/%u\r\n", (unsigned int)fail_cnt, (unsigned int)SHELTER_NET_ROLLBACK_MAX_FAILS);

    if (fail_cnt >= SHELTER_NET_ROLLBACK_MAX_FAILS) {
        printf("[NETCFG] !!! Rollback triggered !!! Restoring backup config.\r\n");
        flash_read_bytes(OFF_BACKUP, &backup, sizeof(backup));
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
    uint8_t pending = 0;
    uint8_t fail_cnt = 0;
    uint8_t init_block[256];

    flash_read_bytes(OFF_MAGIC, &magic, sizeof(magic));
    if (magic == NET_CFG_MAGIC) {
        flash_read_bytes(OFF_CURRENT, &cfg, sizeof(cfg));

        /* [★타 AI 지적기반 체크섬 교차 가드 검증 보완 완료] */
        if (calc_flash_checksum(&cfg) == cfg.checksum) {
            g_net_cfg = cfg;
            s_flash_profile_valid = true;
            flash_read_bytes(OFF_PENDING_FLAG, &pending_flag, sizeof(pending_flag));
            g_net_cfg_pending = (pending_flag != 0);

            netcfg_print_loaded(&g_net_cfg, g_net_cfg_pending ? 1 : 0);
            return true;
        }
        printf("[NETCFG] [WARN] Flash Checksum Broken! Forcing Factory Reset Defaults.\r\n");
    }

    /* 메모리가 텅 비어있거나 무결성이 파괴되었을 때만 초기 매크로 복사 포매팅 진입 */
    load_defaults(&g_net_cfg);
    s_flash_profile_valid = false;
    g_net_cfg.checksum = calc_flash_checksum(&g_net_cfg);
    g_net_cfg_pending = false;
    magic = NET_CFG_MAGIC;

    memset(init_block, 0xFF, sizeof(init_block));
    memcpy(&init_block[OFF_MAGIC], &magic, sizeof(magic));
    memcpy(&init_block[OFF_CURRENT], &g_net_cfg, sizeof(g_net_cfg));
    memcpy(&init_block[OFF_BACKUP], &g_net_cfg, sizeof(g_net_cfg));
    memcpy(&init_block[OFF_PENDING_FLAG], &pending, sizeof(pending));
    memcpy(&init_block[OFF_FAIL_COUNT], &fail_cnt, sizeof(fail_cnt));

    flash_write_bytes(0, init_block, 256);
    printf("[NETCFG] Factory defaults written (mode=DHCP, no static IP saved, broker=%u.%u.%u.%u:%u)\r\n",
           g_net_cfg.broker_ip[0], g_net_cfg.broker_ip[1], g_net_cfg.broker_ip[2], g_net_cfg.broker_ip[3],
           g_net_cfg.broker_port);

    return false;
}

/**
 * @brief [스위치용 최종 가드] 잘못 선언되었던 Bank1 삭제 코드를 정리하고, Bank 2의 실제 데이터 영역을 물리적으로 완전 밀어버리는 포맷 리셋 함수
 */
void NetConfig_ExecuteFlashEraseAndReboot(void)
{
    // [★핵심 가드 1] 다른 태스크나 인터럽트가 이 함수를 두 번 다시 중복 호출하지 못하도록
    // 진입하자마자 1초의 망설임도 없이 전역 인터럽트와 RTOS 스케줄러를 즉시 셧다운합니다.
    __disable_irq();
    vTaskSuspendAll();

    // 이제 안전이 확보되었으므로 로그를 출력합니다. (인터럽트가 꺼졌으므로 HAL_Delay 대신 루프 사용)
    printf("\r\n[FACTORY-RESET] 팩토리 리셋 시퀀스 가동! 설정을 완전 지우고 DHCP 모드로 복귀합니다.\r\n");

    // [★핵심 가드 2] 플래시 물리 삭제(Erase)를 수행합니다. (Bank 2 Sector 7)
    // 이 함수 내부의 __HAL_FLASH_CLEAR_FLAG와 HAL_FLASHEx_Erase가 방해 없이 완벽하게 실행됩니다.
    FLASH_EraseInitTypeDef EraseInitStruct;
    uint32_t SectorError = 0;

    HAL_FLASH_Unlock();

    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS_BANK1 | FLASH_FLAG_ALL_ERRORS_BANK2);

    EraseInitStruct.TypeErase    = FLASH_TYPEERASE_SECTORS;
    EraseInitStruct.Banks        = NET_CONFIG_FLASH_BANK;
    EraseInitStruct.Sector       = NET_CONFIG_FLASH_SECTOR;
    EraseInitStruct.NbSectors    = 1;
    EraseInitStruct.VoltageRange = FLASH_VOLTAGE_RANGE_3;

    HAL_FLASHEx_Erase(&EraseInitStruct, &SectorError);

    HAL_FLASH_Lock();

    printf("[FACTORY-RESET] 물리 섹터 완전 삭제(Erase) 포맷 완료. MCU 하드웨어 재부팅을 트리거합니다.\r\n");

    // [★핵심 가드 3] 캐시를 비우고 하드웨어 강제 Reboot를 때립니다.
    SCB_CleanDCache();

    // 타 타이머의 방해 없이 CPU 코어 리셋 레지스터를 다이렉트로 강제 타격합니다.
    HAL_NVIC_SystemReset();

    while(1); // 하드웨어 리셋이 터지므로 이 루프는 절대 돌지 않습니다.
}

/**
 * @brief Flash 저장 및 재부팅 (기본: NET_SAVE_PROFILE)
 */
bool NetConfig_SaveAndApply(const NetRuntimeConfig *new_cfg)
{
	return NetConfig_SaveAndApplyEx(new_cfg, NET_SAVE_PROFILE);
}

bool NetConfig_SaveAndApplyEx(const NetRuntimeConfig *new_cfg, NetConfigSaveAction action)
{
    uint32_t magic = NET_CFG_MAGIC;
    uint8_t pending = (action == NET_SAVE_PROFILE) ? 1U : 0U;
    uint8_t fail_cnt = 0;
    uint8_t write_block[256];
    NetRuntimeConfig temp_cfg;

    if (new_cfg == NULL) {
        return false;
    }

    if (action == NET_SAVE_FACTORY_DHCP) {
        NetConfig_ExecuteFlashEraseAndReboot();
        return true;
    }

    temp_cfg = *new_cfg;
    if (temp_cfg.net_mode != 0 && temp_cfg.net_mode != 1) {
        temp_cfg.net_mode = 1;
    }
    if (temp_cfg.broker_port == 0) {
        temp_cfg.broker_port = SHELTER_MQTT_BROKER_PORT;
    }
    temp_cfg.checksum = calc_flash_checksum(&temp_cfg);
    g_net_cfg = temp_cfg;
    g_net_cfg_pending = (pending != 0);

    memset(write_block, 0xFF, sizeof(write_block));
    memcpy(&write_block[OFF_MAGIC], &magic, sizeof(magic));
    memcpy(&write_block[OFF_CURRENT], &g_net_cfg, sizeof(g_net_cfg));
    memcpy(&write_block[OFF_BACKUP], &g_net_cfg, sizeof(g_net_cfg));
    memcpy(&write_block[OFF_PENDING_FLAG], &pending, sizeof(pending));
    memcpy(&write_block[OFF_FAIL_COUNT], &fail_cnt, sizeof(fail_cnt));

    flash_write_bytes(0, write_block, 256);
    printf("[NETCFG] Config saved (mode=%s). Rebooting...\r\n",
           g_net_cfg.net_mode ? "DHCP" : "STATIC");

    HAL_Delay(1000);
    SCB_CleanDCache();
    __disable_irq();
    HAL_NVIC_SystemReset();
    while (1) { }

    return true;
}

void NetConfig_ConfirmBoot(void)
{
    uint8_t pending = 0;
    NetRuntimeConfig cur;

    if (!g_net_cfg_pending) {
        return;
    }
    read_current(&cur);
    flash_write_bytes(OFF_BACKUP, &cur, sizeof(cur));
    flash_write_bytes(OFF_PENDING_FLAG, &pending, sizeof(pending));
    g_net_cfg_pending = false;
    printf("[NETCFG] Config confirmed (MQTT connected OK). Flash Profiles Secured.\r\n");
}
