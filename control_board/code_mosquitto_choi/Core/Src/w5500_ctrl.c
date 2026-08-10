#include "w5500_ctrl.h"
#include "config.h"
#include "net_config.h"
#include <stdio.h>
#include <string.h>
#include "cmsis_os2.h"
#include "dhcp.h"
#include "socket.h"

/* --- 로우 레벨 SPI --- */
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

/* --- UID / MAC --- */
void Board_GetDeviceUuid(char *out, size_t out_len)
{
    if (out == NULL || out_len < SHELTER_DEVICE_UID_LEN) {
        return;
    }
    uint32_t w0 = HAL_GetUIDw0();
    uint32_t w1 = HAL_GetUIDw1();
    uint32_t w2 = HAL_GetUIDw2();
    snprintf(out, out_len, "%08X%08X%08X",
             (unsigned)w0, (unsigned)w1, (unsigned)w2);
}

static void W5500_BuildMacFromUid(uint8_t mac[6])
{
    uint32_t uid[3];
    uid[0] = HAL_GetUIDw0();
    uid[1] = HAL_GetUIDw1();
    uid[2] = HAL_GetUIDw2();

    uint8_t hash_mac[3];
    hash_mac[0] = (uint8_t)((uid[0] >> 24) ^ (uid[0] >> 16) ^ (uid[0] >> 8) ^ uid[0] ^
                            (uid[1] >> 16) ^ (uid[2] >> 8));
    hash_mac[1] = (uint8_t)((uid[1] >> 24) ^ (uid[1] >> 16) ^ (uid[1] >> 8) ^ uid[1] ^
                            (uid[2] >> 16) ^ (uid[0] >> 8));
    hash_mac[2] = (uint8_t)((uid[2] >> 24) ^ (uid[2] >> 16) ^ (uid[2] >> 8) ^ uid[2] ^
                            (uid[0] >> 16) ^ (uid[1] >> 8));

    mac[0] = 0x00;
    mac[1] = 0x08;
    mac[2] = 0xDC;
    mac[3] = hash_mac[0];
    mac[4] = hash_mac[1];
    mac[5] = hash_mac[2];
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
    if ((getPHYCFGR() & 0x01) == 0) {
        return false;
    }
    uint8_t ip[4];
    getSIPR(ip);
    if (ip[0] == 0 || ip[0] == 255) {
        return false;
    }
    if (ip_out4 != NULL) {
        memcpy(ip_out4, ip, 4);
    }
    return true;
}

#if SHELTER_NET_USE_DHCP
static uint8_t s_dhcp_buf[1024];
static bool s_dhcp_started = false;

/* [정석 복구] IP 할당 완료 콜백 연산 안전화
 * ★★★ 2026-08-10 추가 수정 ★★★
 * 소켓 번호를 분리(0x22 UDP 충돌)했는데도 여전히 약 40분 주기로 TCP가
 * 끊기는(SR=0x00) 현상이 재현되어 재점검한 결과, 이 콜백이 DHCP
 * "리스 갱신(RENEW)" 때도 매번 wizchip_setnetinfo()를 호출해 SHAR/GAR/
 * SUBR/SIPR — W5500의 "공통(칩 전체) 레지스터" — 를 다시 쓰고 있었습니다.
 * 이 레지스터들은 모든 소켓이 공유하는데, IP가 실제로 바뀌지 않은
 * 일반적인 리스 갱신에서도 매번 재작성하면 이미 ESTABLISHED 상태인
 * MQTT TCP 소켓이 깨질 수 있습니다(소켓 번호 충돌 없이도).
 * → 값이 실제로 바뀌었을 때만 재작성하도록 수정. */
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
    if (memcmp(cur.ip, ni.ip, 4) == 0 && memcmp(cur.gw, ni.gw, 4) == 0 &&
        memcmp(cur.sn, ni.sn, 4) == 0 && memcmp(cur.mac, ni.mac, 6) == 0) {
        printf("[W5500][DHCP] Lease renewed, IP unchanged (%d.%d.%d.%d) — register rewrite SKIPPED (MQTT 소켓 보호)\r\n",
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

static bool W5500_ApplyDhcp(void)
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
 * @brief FreeRTOS 1초 주기 태스크(StartAppTimeTask)에서 상시 호출되는 DHCP 핵심 엔진
 *
 * ★★★ [2026-08-09 근본 원인 확정 및 수정] ★★★
 * "40분마다 MQTT 접속이 끊기고 이후 재접속이 안 되는" 문제의 진짜 원인은
 * 이 함수의 로직이 아니라 **소켓 번호 충돌**이었습니다.
 *
 *   config.h:  #define SHELTER_DHCP_SOCKET_NUM  0
 *   app.h   :  #define MQTT_SOCKET_NUM          0
 *
 * DHCP 클라이언트와 MQTT 클라이언트가 **똑같이 소켓 0번**을 사용하고
 * 있었습니다. DHCP 리스(lease) 갱신 주기(보통 임대시간의 50% 지점, T1
 * 타이머)가 되면 DHCP_run() 내부에서 socket(0, Sn_MR_UDP, ...)를 호출해
 * 소켓 0번을 UDP 모드로 재오픈하는데, 이 순간 그 소켓을 TCP로 붙잡고
 * 있던 MQTT 연결이 그대로 파괴됩니다. 실제 현장 로그에도
 * "[DROP] TCP Socket Lost (SR=0x22)" 가 찍혔는데, 0x22는 SOCK_UDP
 * 상태값입니다 — MQTT 소켓이 DHCP에 의해 UDP 모드로 뒤바뀐 것이 직접
 * 증거입니다. 이전에 시도했던 "IP를 획득한 뒤 30분간 DHCP_run() 호출을
 * 건너뛰는" 방식은 충돌 시점을 뒤로 늦출 뿐 근본 원인을 없애지 못했고
 * (그래서 정확히 "40분 전후"에 여전히 문제가 재현됨), 오히려 진짜 갱신
 * 타이밍을 놓쳐 리스가 만료될 위험까지 있었습니다.
 *
 * [최종 조치] config.h의 SHELTER_DHCP_SOCKET_NUM을 3번으로 분리했습니다
 * (0=MQTT, 1=SNTP, 2=DNS, 3=DHCP 전용, 4~7 여유). 이제 소켓이 겹치지
 * 않으므로 매초 DHCP_time_handler()/DHCP_run()을 그냥 정상적으로
 * 호출해도 MQTT 소켓과 절대 충돌하지 않습니다. 따라서 임시 30분 가드
 * 로직은 제거하고 WIZnet 권장 표준 패턴으로 복원했습니다.
 */
void W5500_DhcpTick(void)
{
    if (!s_dhcp_started) return;
    if ((getPHYCFGR() & 0x01) == 0) return; // 케이블 분리 시 스킵

    DHCP_time_handler();
    uint8_t ret = DHCP_run();

    if (ret == DHCP_IP_ASSIGN || ret == DHCP_IP_CHANGED) {
        W5500_PrintNetwork(ret == DHCP_IP_ASSIGN ? "DHCP ASSIGN" : "DHCP RENEW/CHANGED");
    } else if (ret == DHCP_FAILED) {
        printf("[W5500][DHCP] Lease 갱신 실패 — 재시도 대기\r\n");
    }
    /* DHCP_IP_LEASED(정상 유지) 는 별도 처리 불필요 */
}

#endif /* SHELTER_NET_USE_DHCP */

#if SHELTER_NET_USE_STATIC
static bool W5500_ApplyStatic(void)
{
    /* ★ 2026-08-10: config.h 하드코딩 매크로 대신 EEPROM(net_config.c)에서
       로드된 g_net_cfg를 사용 — 웹서버에서 IP 변경 가능해짐.
       g_net_cfg는 main()이 W5500_Init() 이전에 NetConfig_Load()로 채워둠. */
    wiz_NetInfo ni;
    memset(&ni, 0, sizeof(ni));
    W5500_BuildMacFromUid(ni.mac);
    memcpy(ni.ip, g_net_cfg.ip, 4);
    memcpy(ni.sn, g_net_cfg.sn, 4);
    memcpy(ni.gw, g_net_cfg.gw, 4);
    memcpy(ni.dns, g_net_cfg.dns, 4);
    ni.dhcp = NETINFO_STATIC;
    wizchip_setnetinfo(&ni);
    W5500_PrintNetwork("STATIC OK");
    return true;
}
#endif

/**
 * @brief EEPROM 런타임 설정 데이터에 따라 DHCP 또는 STATIC 네트워크를 동적으로 적용합니다.
 */
bool W5500_ApplyStatic(void)
{
    wiz_NetInfo ni;

    // 안전장치: 구조체 초기화 및 MAC 주소 바인딩
    memset(&ni, 0, sizeof(ni));
    W5500_BuildMacFromUid(ni.mac);

    // ★ [핵심] config.h 매크로 대신 EEPROM 런타임 변수(g_net_cfg)의 주소 데이터를 다이렉트 매핑
    memcpy(ni.ip,  g_net_cfg.ip,  4);
    memcpy(ni.sn,  g_net_cfg.sn,  4);
    memcpy(ni.gw,  g_net_cfg.gw,  4);
    memcpy(ni.dns, g_net_cfg.dns, 4);
    ni.dhcp = NETINFO_STATIC;

    // W5500 하드웨어 레지스터 칩에 정적 IP 영구 적용
    wizchip_setnetinfo(&ni);

    // 기존 보드 고유의 정적 적용 완료 로깅 함수 호출
    W5500_PrintNetwork("STATIC OK");

    // 반영된 정보 정밀 로깅 추가
    printf("[W5500] Run-time Static IP Fixed -> %u.%u.%u.%u\r\n",
           ni.ip[0], ni.ip[1], ni.ip[2], ni.ip[3]);

    return true;
}
bool W5500_ApplyNetwork(void)
{
    // 예외 처리 안전장치: 최초 공장 출하 상태이거나 데이터 깨짐으로 인해
    // mode 값이 0 또는 1이 아닐 경우, 통신 다운을 방지하기 위해 보수적 DHCP 모드로 강제 구동합니다.
    if (g_net_cfg.net_mode != 0 && g_net_cfg.net_mode != 1) {
        printf("[W5500] Warning: Invalid 런타임 net_mode (%d). Falling back to DHCP Default.\r\n", g_net_cfg.net_mode);
        return W5500_ApplyDhcp();
    }

    // 런타임 동적 스위칭 조건 분기
    if (g_net_cfg.net_mode == 0) {
        /* [서버의 원격 명령 지정 STATIC IP 모드] */
        printf("[W5500] Switching to 런타임 STATIC Mode via Server Command\r\n");
        return W5500_ApplyStatic();
    }
    else {
        /* [최초 공장 부팅 및 일반 DHCP 모드] */
        printf("[W5500] Switching to 런타임 DHCP Mode via Default Status\r\n");
        return W5500_ApplyDhcp();
    }
}

void W5500_Interrupt_Config(void) {
    setIMR(0xFF);
    setSn_IMR(0, Sn_IR_RECV);
}

void W5500_Init(void) {
    printf("--- W5500 init ---\r\n");

    HAL_GPIO_WritePin(WIZ_RST_GPIO_Port, WIZ_RST_Pin, GPIO_PIN_RESET);
    for(volatile uint32_t i=0; i<500000; i++);
    HAL_GPIO_WritePin(WIZ_RST_GPIO_Port, WIZ_RST_Pin, GPIO_PIN_SET);
    for(volatile uint32_t i=0; i<2500000; i++);

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
