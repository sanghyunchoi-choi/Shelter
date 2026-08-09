/**
 * @file config.h
 * @brief Smart Shelter 제어보드 — 네트워크·MQTT 기본 설정 (빌드 전 여기만 수정)
 *
 * IP는 모두 {a, b, c, d} 형식 한 줄입니다.
 */
#ifndef SHELTER_CONFIG_H
#define SHELTER_CONFIG_H

/* =============================================================================
 * 1) LAN 모드 — 아래 둘 중 하나만 1
 * ============================================================================= */
#define SHELTER_NET_USE_STATIC   0
#define SHELTER_NET_USE_DHCP     1

#if (SHELTER_NET_USE_STATIC + SHELTER_NET_USE_DHCP) != 1
#error "config.h: STATIC 또는 DHCP 중 정확히 하나만 1"
#endif

/* Static일 때만 사용 (DHCP면 무시됨) */
#define SHELTER_NET_IP           {192, 168, 0, 50}			// 제어 보드 IP
#define SHELTER_NET_SUBNET       {255, 255, 255, 0}
#define SHELTER_NET_GATEWAY      {192, 168, 0, 1}
#define SHELTER_NET_DNS          {8, 8, 8, 8}

/* DHCP일 때만 사용 */
#define SHELTER_DHCP_TIMEOUT_MS  30000U
#define SHELTER_DHCP_SOCKET_NUM  0

/* =============================================================================
 * 2) MQTT 브로커 (제어보드가 접속하는 PC/서버)
 * ============================================================================= */
//#define SHELTER_MQTT_BROKER_IP   {192, 168, 50, 220}
//#define SHELTER_MQTT_BROKER_IP   {192, 168, 50, 177}
#define SHELTER_MQTT_BROKER_IP   {192, 168, 0, 107}
#define SHELTER_MQTT_BROKER_PORT 1883

/* =============================================================================
 * 기타
 * ============================================================================= */
#define SHELTER_DEVICE_UID_LEN   25   /* 96-bit UID → 24 HEX + NUL */

/* =============================================================================
 * 3) RS485 (AC/AP) — ACK 타임아웃·디버그
 * ============================================================================= */
#define SHELTER_RS485_TURNAROUND_MS   3    /* TX 후 DE→RX 전환 대기 */
#define SHELTER_RS485_ACK_RETRY_MS    50   /* 1회 대기 간격 */
#define SHELTER_RS485_ACK_RETRIES     20   /* 총 ~1s (수동 테스트보다 여유) */
#define SHELTER_RS485_DEBUG_LOG       0    /* 1=[AP]/[AC] TX·RX·timeout 등 alive-check 로그 */
#define SHELTER_RS485_LOG_EACH_BYTE   0    /* 1=UART ISR 바이트별 로그 (SHELTER_RS485_DEBUG_LOG=1 일 때만 의미) */

#endif /* SHELTER_CONFIG_H */
