/**
 * @file w5500_ctrl.c
 * @brief W5500 하드웨어 드라이버 및 DHCP/STATIC 런타임 스위칭 엔진 (1차부)
 */
#include "w5500_ctrl.h"
#include "config.h"
#include "net_config.h"
#include <stdio.h>
#include <string.h>
#include "cmsis_os2.h"
#include "dhcp.h"
#include "socket.h"

extern SPI_HandleTypeDef hspi1;

static uint8_t s_dhcp_buf[1024];
static bool s_dhcp_started = false;
static bool s_prov_mode = false;

void W5500_SetProvisioningMode(bool active)
{
	s_prov_mode = active;
}

bool W5500_IsProvisioningMode(void)
{
	return s_prov_mode;
}

static void W5500_ForcePhy100MFull(void)
{
	wiz_PhyConf phyconf;

	phyconf.by = PHY_CONFBY_SW;
	phyconf.mode = PHY_MODE_MANUAL;
	phyconf.speed = PHY_SPEED_100;
	phyconf.duplex = PHY_DUPLEX_FULL;
	wizphy_setphyconf(&phyconf);
	for (int i = 0; i < 30; i++) {
		if ((getPHYCFGR() & 0x01) != 0) {
			break;
		}
		HAL_Delay(100);
	}
	printf("[W5500] PHY forced 100M Full, link=%s\r\n",
	       ((getPHYCFGR() & 0x01) != 0) ? "UP" : "DOWN");
}

/* --- 로우 레벨 SPI 버스 제어 인터페이스 --- */
void W5500_Select(void) {
    HAL_GPIO_WritePin(WIZ_CS_GPIO_Port, WIZ_CS_Pin, GPIO_PIN_RESET);
}

void W5500_Deselect(void) {
    HAL_GPIO_WritePin(WIZ_CS_GPIO_Port, WIZ_CS_Pin, GPIO_PIN_SET);
}

uint8_t W5500_ReadByte(void) {
    uint8_t rb = 0;
    HAL_SPI_Receive(&hspi1, &rb, 1, HAL_MAX_DELAY);
    return rb;
}

void W5500_WriteByte(uint8_t wb) {
    HAL_SPI_Transmit(&hspi1, &wb, 1, HAL_MAX_DELAY);
}

void W5500_ReadBurst(uint8_t* pBuf, uint16_t len) {
    HAL_SPI_Receive(&hspi1, pBuf, len, HAL_MAX_DELAY);
}

void W5500_WriteBurst(uint8_t* pBuf, uint16_t len) {
    HAL_SPI_Transmit(&hspi1, pBuf, len, HAL_MAX_DELAY);
}

/* --- 고유 UID 파싱 및 하드웨어 고유 MAC 매핑 --- */
void Board_GetDeviceUuid(char *out, size_t out_len)
{
    if (out == NULL || out_len < SHELTER_DEVICE_UID_LEN) return;
    uint32_t w0 = HAL_GetUIDw0();
    uint32_t w1 = HAL_GetUIDw1();
    uint32_t w2 = HAL_GetUIDw2();
    snprintf(out, out_len, "%08X%08X%08X", (unsigned)w0, (unsigned)w1, (unsigned)w2);
}

static void W5500_BuildMacFromUid(uint8_t mac[6])
{
    uint32_t uid[3];
    uid[0] = HAL_GetUIDw0();
    uid[1] = HAL_GetUIDw1();
    uid[2] = HAL_GetUIDw2();

    uint8_t hash_mac[3];
    hash_mac[0] = (uint8_t)((uid[0] >> 24) ^ (uid[0] >> 16) ^ (uid[0] >> 8) ^ uid[0] ^ (uid[1] >> 16) ^ (uid[2] >> 8));
    hash_mac[1] = (uint8_t)((uid[1] >> 24) ^ (uid[1] >> 16) ^ (uid[1] >> 8) ^ uid[1] ^ (uid[2] >> 16) ^ (uid[0] >> 8));
    hash_mac[2] = (uint8_t)((uid[2] >> 24) ^ (uid[2] >> 16) ^ (uid[2] >> 8) ^ uid[2] ^ (uid[0] >> 16) ^ (uid[1] >> 8));

    mac[0] = 0x00; mac[1] = 0x08; mac[2] = 0xDC;
    mac[3] = hash_mac[0]; mac[4] = hash_mac[1]; mac[5] = hash_mac[2];
}

void W5500_PrintNetwork(const char *tag)
{
    wiz_NetInfo ni;
    wizchip_getnetinfo(&ni);
    printf("[W5500] %s MAC %02X:%02X:%02X:%02X:%02X:%02X | IP %d.%d.%d.%d | %s\r\n",
           tag ? tag : "NET",
           ni.mac[0], ni.mac[1], ni.mac[2], ni.mac[3], ni.mac[4], ni.mac[5],
           ni.ip[0], ni.ip[1], ni.ip[2], ni.ip[3],
           (ni.dhcp == NETINFO_DHCP) ? "DHCP" : "STATIC");
}

bool W5500_NetworkReady(uint8_t *ip_out4)
{
    if ((getPHYCFGR() & 0x01) == 0) return false;
    uint8_t ip[4];
    getSIPR(ip);
    if (ip[0] == 0 || ip[0] == 255) return false;
    if (ip_out4 != NULL) memcpy(ip_out4, ip, 4);
    return true;
}

/* --- DHCP 콜백 및 구동 제어 엔진 영역 --- */
static void cb_dhcp_ip_assign(void) {
    wiz_NetInfo ni;
    getIPfromDHCP(ni.ip);
    getGWfromDHCP(ni.gw);
    getSNfromDHCP(ni.sn);
    getDNSfromDHCP(ni.dns);
    ni.dhcp = NETINFO_DHCP;
    W5500_BuildMacFromUid(ni.mac);

    wiz_NetInfo cur;
    wizchip_getnetinfo(&cur);

    // [★MQTT 소켓 생명 연장 정석 가드 유지] 리스 갱신 시 값 미변경이면 하드웨어 주입 패스
    if (memcmp(cur.ip, ni.ip, 4) == 0 && memcmp(cur.gw, ni.gw, 4) == 0 &&
        memcmp(cur.sn, ni.sn, 4) == 0 && memcmp(cur.mac, ni.mac, 6) == 0) {
        printf("[W5500][DHCP] Lease renewed, IP unchanged (%d.%d.%d.%d) — register rewrite SKIPPED\r\n",
               ni.ip[0], ni.ip[1], ni.ip[2], ni.ip[3]);
        return;
    }

    printf("[W5500][DHCP] Network info actually changed (%d.%d.%d.%d -> %d.%d.%d.%d) — applying\r\n",
           cur.ip[0], cur.ip[1], cur.ip[2], cur.ip[3], ni.ip[0], ni.ip[1], ni.ip[2], ni.ip[3]);
    wizchip_setnetinfo(&ni);
}

static void cb_dhcp_ip_conflict(void) {
    printf("[W5500] DHCP IP Conflict!\r\n");
}

bool W5500_ApplyDhcp(void)
{
    wiz_NetInfo ni;
    uint8_t zero[4] = {0, 0, 0, 0};

    memset(&ni, 0, sizeof(ni));
    W5500_BuildMacFromUid(ni.mac);
    memcpy(ni.ip, zero, 4);
    memcpy(ni.sn, zero, 4);
    memcpy(ni.gw, zero, 4);
    memcpy(ni.dns, zero, 4);
    ni.dhcp = NETINFO_DHCP;
    wizchip_setnetinfo(&ni);

    close(SHELTER_DHCP_SOCKET_NUM);
    reg_dhcp_cbfunc(cb_dhcp_ip_assign, cb_dhcp_ip_assign, cb_dhcp_ip_conflict);

    DHCP_init(SHELTER_DHCP_SOCKET_NUM, s_dhcp_buf);
    s_dhcp_started = true;

    printf("[W5500] DHCP Engine Armed. Passing control to FreeRTOS Loop...\r\n");
    return true;
}
/**
 * @file w5500_ctrl.c
 * @brief W5500 하드웨어 드라이버 및 DHCP/STATIC 런타임 스위칭 엔진 (2차부)
 */

/* 1초 주기 FreeRTOS OS 태스크 타이머 핸들러 */
void W5500_DhcpTick(void)
{
	if (s_prov_mode) {
		return;
	}
	// [★런타임 동적 가드] 내장 플래시 로드 모드가 STATIC(0) 상태이면, DHCP 틱 연산을 즉시 패스합니다.
    if (g_net_cfg.net_mode == 0) return;

    if (!s_dhcp_started) return;
    if ((getPHYCFGR() & 0x01) == 0) return; // 이더넷 케이블 단선 시 연산 연기 스킵

    DHCP_time_handler();
    uint8_t ret = DHCP_run();

    if (ret == DHCP_IP_ASSIGN || ret == DHCP_IP_CHANGED) {
        W5500_PrintNetwork(ret == DHCP_IP_ASSIGN ? "DHCP ASSIGN" : "DHCP RENEW/CHANGED");
    } else if (ret == DHCP_FAILED) {
        printf("[W5500][DHCP] Lease 갱신 실패 — 재시도 대기\r\n");
    }
}

bool W5500_ApplyStatic(void)
{
    wiz_NetInfo ni;
    memset(&ni, 0, sizeof(ni));
    W5500_BuildMacFromUid(ni.mac);

    // ★ [핵심] config.h 매크로 상수를 전면 타파하고, 내장 플래시 런타임 변수(g_net_cfg)를 실시간 복사 적용!
    memcpy(ni.ip,  g_net_cfg.ip,  4);
    memcpy(ni.sn,  g_net_cfg.sn,  4);
    memcpy(ni.gw,  g_net_cfg.gw,  4);
    memcpy(ni.dns, g_net_cfg.dns, 4);
    ni.dhcp = NETINFO_STATIC;

    wizchip_setnetinfo(&ni);
    W5500_PrintNetwork("STATIC OK");
    return true;
}

bool W5500_ApplyProvisioningNetwork(void)
{
    W5500_ForcePhy100MFull();

    wiz_NetInfo ni;
    uint8_t prov_ip[] = SHELTER_PROV_IP;
    uint8_t prov_sn[] = SHELTER_PROV_SUBNET;
    uint8_t prov_gw[] = SHELTER_PROV_GATEWAY;
    uint8_t prov_dns[] = SHELTER_PROV_DNS;

    memset(&ni, 0, sizeof(ni));
    W5500_BuildMacFromUid(ni.mac);
    memcpy(ni.ip, prov_ip, 4);
    memcpy(ni.sn, prov_sn, 4);
    memcpy(ni.gw, prov_gw, 4);
    memcpy(ni.dns, prov_dns, 4);
    ni.dhcp = NETINFO_STATIC;
    wizchip_setnetinfo(&ni);
    W5500_PrintNetwork("PROV SETUP");
    return true;
}

void W5500_StopDhcp(void)
{
    if (!s_dhcp_started) {
        return;
    }
    DHCP_stop();
    close(SHELTER_DHCP_SOCKET_NUM);
    s_dhcp_started = false;
    printf("[W5500] DHCP stopped for provisioning mode\r\n");
}

/**
 * @brief  [★런타임 동적 분기 네트워크 코어]
 *         내장 플래시 로드 상태에 맞춰 DHCP 엔진 작동 혹은 고정 IP 주입을 실시간 지휘 제어합니다.
 */
bool W5500_ApplyNetwork(void)
{
    // 최초 공장 출하 시그널 혹은 예외 오염 상태 방지용 보수적 안전장치
    if (g_net_cfg.net_mode != 0 && g_net_cfg.net_mode != 1) {
        printf("[W5500] Warning: Invalid net_mode (%d). Force Falling back to DHCP Default.\r\n", g_net_cfg.net_mode);
        return W5500_ApplyDhcp();
    }

    if (g_net_cfg.net_mode == 0) {
        printf("[W5500] 런타임 모드 판독 결과 ➔ STATIC 고정 기어 변속 적용\r\n");
        return W5500_ApplyStatic();
    }
    else {
        printf("[W5500] 런타임 모드 판독 결과 ➔ DHCP 자동 할당 기어 변속 적용\r\n");
        return W5500_ApplyDhcp();
    }
}

void W5500_Interrupt_Config(void) {
    setIMR(0xFF);
    setSn_IMR(0, Sn_IR_RECV);
}

void W5500_Init(void) {
    printf("--- W5500 init ---\r\n");

    // W5500 하드웨어 하이/로우 물리 리셋 펄스 시전
    HAL_GPIO_WritePin(WIZ_RST_GPIO_Port, WIZ_RST_Pin, GPIO_PIN_RESET);
    for(volatile uint32_t i=0; i<500000; i++);
    HAL_GPIO_WritePin(WIZ_RST_GPIO_Port, WIZ_RST_Pin, GPIO_PIN_SET);
    for(volatile uint32_t i=0; i<2500000; i++);

    // WIZnet ioLibrary SPI 기본 콜백 바인딩 연결
    reg_wizchip_cs_cbfunc(W5500_Select, W5500_Deselect);
    reg_wizchip_spi_cbfunc(W5500_ReadByte, W5500_WriteByte);
    reg_wizchip_spiburst_cbfunc(W5500_ReadBurst, W5500_WriteBurst);

    uint8_t memsize[2][8] = {{2,2,2,2,2,2,2,2}, {2,2,2,2,2,2,2,2}};
    if (ctlwizchip(CW_INIT_WIZCHIP, (void*)memsize) == -1) {
        printf("[W5500] memory alloc failed\r\n");
        return;
    }

    if (getVERSIONR() != 0x04) {
        printf("[W5500] not found (read 0x%02X)\r\n", getVERSIONR());
    } else {
        printf("[W5500] Chip version verified: 0x04\r\n");
    }
}
