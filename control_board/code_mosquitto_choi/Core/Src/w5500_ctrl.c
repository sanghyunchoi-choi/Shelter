#include "w5500_ctrl.h"
#include "config.h"
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

/* [정석 복구] IP 할당 완료 콜백 연산 안전화 */
static void cb_dhcp_ip_assign(void) {
    wiz_NetInfo ni;
    getIPfromDHCP(ni.ip);
    getGWfromDHCP(ni.gw);
    getSNfromDHCP(ni.sn);
    getDNSfromDHCP(ni.dns);
    ni.dhcp = NETINFO_DHCP;
    W5500_BuildMacFromUid(ni.mac);
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
 */
void W5500_DhcpTick(void)
{
    // DHCP 기능이 기동되지 않았거나 랜 케이블이 분리된 상태라면 무조건 스킵
    if (!s_dhcp_started) return;
    if ((getPHYCFGR() & 0x01) == 0) return;

    // 1. W5500 DHCP 라이브러리 내부 핵심 시계(초 카운터)는 1초마다 무조건 갱신되어야 합니다.
    DHCP_time_handler();

    // 2. 외부 라이브러리 링크 에러를 차단하기 위한 우리 측 고유 1초 정적 누적 카운터
    static uint32_t leased_seconds = 0;

    // 3. W5500 내부 IP 레지스터를 실시간으로 직접 조회하여 현재 개통 상태를 체크
    uint8_t current_ip[4];
    getSIPR(current_ip);

    // [교정 핵심] 유효한 사설 IP(192.168.x.x 등 0과 255가 아닌 진짜 IP)를 이미 들고 있는 상황에서만 가드를 켭니다!
    if (current_ip[0] != 0 && current_ip[0] != 255) {

        leased_seconds++; // 1초씩 누적 타이머 가동

        /*
         * [코어 가드 규칙]
         * IP를 성공적으로 들고 있는 안정을 취한 상태라면, 최소 30분(1800초) 동안은
         * 백그라운드 DHCP_run()의 불필요한 구동을 원천 차단하여 MQTT 소켓을 100% 보호합니다.
         */
        if (leased_seconds < 1800) {
            return; // 30분 임계점 도달 전까지는 얌전하게 침묵 유지 (MQTT 간섭 0%)
        }
    } else {
        // 부팅 직후이거나 IP가 0.0.0.0 혹은 유실된 상태라면 가드를 가동하지 않고
        // 아래의 DHCP_run()으로 즉시 직결 통과시켜서 공유기한테 IP를 얻어오게 만듭니다.
        leased_seconds = 0;
    }

    /*
     * 4. 30분 마진이 초과했거나, 부팅 직후 최초 IP가 없는 단계일 때만 문이 열려 진입합니다.
     *    공유기(DHCP 서버)단에 연장 신청(REQUEST) 또는 최초 할당(DISCOVER) 패킷을 송신합니다.
     */
    uint8_t ret = DHCP_run();

    // 공유기 수락으로 최초 할당 또는 연장 계약이 무사히 체결 완료된 시점
    if (ret == DHCP_IP_ASSIGN || ret == DHCP_IP_CHANGED) {
        W5500_PrintNetwork("DHCP SUCCESS");

        // IP 획득에 성공했으므로 카운터를 다시 0으로 비워 다음 30분간 MQTT 소켓을 완벽 차단 보호함
        leased_seconds = 0;
    }
}






#endif /* SHELTER_NET_USE_DHCP */

#if SHELTER_NET_USE_STATIC
static bool W5500_ApplyStatic(void)
{
    const uint8_t ip[4]  = SHELTER_NET_IP;
    const uint8_t sn[4] = SHELTER_NET_SUBNET;
    const uint8_t gw[4] = SHELTER_NET_GATEWAY;
    const uint8_t dns[4]= SHELTER_NET_DNS;

    wiz_NetInfo ni;
    memset(&ni, 0, sizeof(ni));
    W5500_BuildMacFromUid(ni.mac);
    memcpy(ni.ip, ip, 4);
    memcpy(ni.sn, sn, 4);
    memcpy(ni.gw, gw, 4);
    memcpy(ni.dns, dns, 4);
    ni.dhcp = NETINFO_STATIC;
    wizchip_setnetinfo(&ni);
    W5500_PrintNetwork("STATIC OK");
    return true;
}
#endif

bool W5500_ApplyNetwork(void)
{
#if SHELTER_NET_USE_DHCP
    return W5500_ApplyDhcp();
#else
    return W5500_ApplyStatic();
#endif
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
