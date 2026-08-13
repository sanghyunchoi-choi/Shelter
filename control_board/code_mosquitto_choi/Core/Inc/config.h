/**
 * @file config.h
 * @brief Smart Shelter 제어보드 — 공장 기본값 (런타임 설정은 Flash / 직결 HTTP)
 *
 * ★ 양산: 펌웨어 1종 빌드 → PC 직결 http://192.168.0.100 에서 LAN·브로커 설정 (섹션 2C)
 * ★ 아래 값은 Flash가 비어 있을 때만 쓰이는 공장 출하 기본값입니다.
 * IP는 모두 {a, b, c, d} 형식 한 줄입니다.
 */
#ifndef SHELTER_CONFIG_H
#define SHELTER_CONFIG_H

/* =============================================================================
 * 1) LAN 모드 — 공장 출하 기본: DHCP (현장 LAN에서 IP 자동 획득)
 *    STATIC/DHCP 최종 모드는 직결 HTTP 또는 Flash 저장값이 우선합니다.
 * ============================================================================= */
#define SHELTER_NET_USE_STATIC   0
#define SHELTER_NET_USE_DHCP     1

#if (SHELTER_NET_USE_STATIC + SHELTER_NET_USE_DHCP) != 1
#error "config.h: STATIC 또는 DHCP 중 정확히 하나만 1"
#endif

/* Static일 때 Flash/직결 설정 없을 때만 사용 */
#define SHELTER_NET_IP           {192, 168, 0, 50}			// 제어 보드 IP
#define SHELTER_NET_SUBNET       {255, 255, 255, 0}
#define SHELTER_NET_GATEWAY      {192, 168, 0, 1}
#define SHELTER_NET_DNS          {8, 8, 8, 8}

/* DHCP일 때만 사용 */
#define SHELTER_DHCP_TIMEOUT_MS  30000U
/* ★★★ 2026-08-09 핵심 버그 수정 ★★★
 * 기존 값 0은 app.h의 MQTT_SOCKET_NUM(=0)과 완전히 동일한 소켓이었습니다.
 * DHCP 리스 갱신 시점(약 30~40분 주기, 임대시간의 T1 지점)마다 DHCP
 * 라이브러리가 소켓 0번을 UDP 모드로 재오픈하면서 그 소켓을 TCP로 쓰던
 * MQTT 연결을 그대로 파괴하는 것이 "40분마다 접속 끊김" 현상의 진짜
 * 원인이었습니다(로그의 "SR=0x22" = SOCK_UDP가 그 직접 증거).
 * MQTT=0, SNTP=1, DNS=2 이므로 DHCP는 겹치지 않는 3번을 사용합니다. */
#define SHELTER_DHCP_SOCKET_NUM  3

/* =============================================================================
 * 2) MQTT 브로커 — Flash/직결 설정 없을 때만 사용하는 공장 기본값
 * ============================================================================= */
//#define SHELTER_MQTT_BROKER_IP   {192, 168, 50, 220}
//#define SHELTER_MQTT_BROKER_IP   {192, 168, 50, 177}
#define SHELTER_MQTT_BROKER_IP   {192, 168, 0, 107}
#define SHELTER_MQTT_BROKER_PORT 1883

/* =============================================================================
 * 2-1) Flash 저장 + 자동 롤백 (내부 Flash Bank2 Sector7, net_config.c)
 * ============================================================================= */
#define SHELTER_NET_EEPROM_BASE_ADDR    0x0000
#define SHELTER_NET_ROLLBACK_MAX_FAILS  3
#define SHELTER_NET_ROLLBACK_TIMEOUT_MS 60000U

/* =============================================================================
 * 2-2) [v2.1] PC 직결 프로비저닝 — 기본 IP 192.168.0.100
 *  PC NIC 예: 192.168.0.10 / 255.255.255.0 → 브라우저 http://192.168.0.100
 * ============================================================================= */
#define SHELTER_PROV_IP           {192, 168, 0, 100}
#define SHELTER_PROV_SUBNET       {255, 255, 255, 0}
#define SHELTER_PROV_GATEWAY      {192, 168, 0, 1}
#define SHELTER_PROV_DNS          {8, 8, 8, 8}
#define SHELTER_PROV_TRIGGER_MS           90000U   /* Flash 없음: MQTT 실패 후 prov */
#define SHELTER_PROV_LINKUP_MS            5000U    /* 재설정·공장: 링크 UP → prov (5초) */
#define SHELTER_PROV_AFTER_SAVE_MS       120000U   /* 저장 직후 1회: LAN DHCP 유예 2분 */
#define SHELTER_PROV_AFTER_SAVE_DEADLINE_MS 180000U /* 저장 직후 1회: prov fallback 3분 */
#define SHELTER_HTTP_SOCKET_NUM   4        /* MQTT=0 SNTP=1 DNS=2 DHCP=3 HTTP=4 */

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
#define SHELTER_RS485_OFFLINE_RETRIES 4    /* 미연결 장치: ~200ms 후 포기 (태스크 블로킹 방지) */
#define SHELTER_RS485_DEBUG_LOG       0    /* 1=[AP]/[AC] TX·RX·timeout 등 alive-check 로그 */
#define SHELTER_RS485_LOG_EACH_BYTE   0    /* 1=UART ISR 바이트별 로그 (SHELTER_RS485_DEBUG_LOG=1 일 때만 의미) */

#endif /* SHELTER_CONFIG_H */
