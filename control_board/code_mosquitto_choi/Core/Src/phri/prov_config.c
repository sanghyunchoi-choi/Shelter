/**
 * @file prov_config.c
 * @brief IP 카메라 방식 직결 설정 — http://192.168.0.100
 */
#include "prov_config.h"
#include "config.h"
#include "net_config.h"
#include "w5500_ctrl.h"
#include "main.h"
#include "socket.h"
#include "httpServer.h"
#include "httpParser.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static volatile bool s_prov_active = false;
static uint8_t s_http_tx[2048];
static uint8_t s_http_rx[2048];
static uint8_t s_http_sock_list[1] = { SHELTER_HTTP_SOCKET_NUM };
static bool s_http_inited = false;

static const char PROV_INDEX_HTML[] =
"<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>Shelter Setup</title>"
"<style>body{font-family:sans-serif;background:#0f1117;color:#f1f5f9;padding:20px;max-width:480px;margin:auto}"
"input,select{width:100%;padding:8px;margin:4px 0 12px;background:#1e2230;border:1px solid #334155;color:#fff;border-radius:6px;box-sizing:border-box}"
"label{font-size:12px;color:#94a3b8}button{background:#6366f1;color:#fff;border:0;padding:12px;border-radius:8px;width:100%;font-size:15px;cursor:pointer}"
"h1{color:#a3e635;font-size:18px}.hint{font-size:12px;color:#64748b;line-height:1.5;margin-bottom:14px}"
".row{display:grid;grid-template-columns:1fr 1fr;gap:8px}</style></head><body>"
"<h1>Smart Shelter 네트워크 설정</h1>"
"<p class=\"hint\">공장·현장 최초 설정 (직결 프로비저닝)<br>보드 IP: <b>192.168.0.100</b><br>"
"PC를 <b>192.168.0.10</b> / 255.255.255.0 으로 설정 후 이 페이지에서 저장하세요.<br>"
"<b>일반 현장:</b> LAN 모드 <b>DHCP</b> + MQTT 브로커 IP만 입력 (IP 필드는 무시됨).<br>"
"<b>고정 IP(.50 등):</b> LAN 모드를 <b>STATIC</b>으로 선택해야 IP가 저장됩니다.</p>"
"<form action=\"save.cgi\" method=\"POST\">"
"<label>LAN 모드</label><select name=\"mode\"><option value=\"dhcp\" selected>DHCP (권장)</option><option value=\"static\">STATIC (고정 IP)</option></select>"
"<label>IP 주소</label><input name=\"ip\" value=\"192.168.0.50\" placeholder=\"192.168.0.50\">"
"<label>서브넷</label><input name=\"sn\" value=\"255.255.255.0\">"
"<label>게이트웨이</label><input name=\"gw\" value=\"192.168.0.1\">"
"<label>DNS</label><input name=\"dns\" value=\"8.8.8.8\">"
"<label>MQTT 브로커 IP</label><input name=\"broker\" value=\"192.168.0.107\">"
"<label>MQTT 포트</label><input name=\"port\" value=\"1883\">"
"<button type=\"submit\">저장 및 재부팅</button></form>"
"<p class=\"hint\" style=\"margin-top:16px\"><a href=\"status.cgi\" style=\"color:#818cf8\">현재 설정 보기</a></p>"
"</body></html>";

static bool parse_ipv4_param(char *http_packet, const char *key, uint8_t out[4])
{
	uint8_t *val = get_http_param_value(http_packet, (char *)key);
	if (!val || !val[0]) {
		return false;
	}
	inet_addr_(val, out);
	return (out[0] != 0 || out[1] != 0 || out[2] != 0 || out[3] != 0);
}

static void prov_force_http_listen(void)
{
	uint8_t sn = SHELTER_HTTP_SOCKET_NUM;

	close(sn);
	while (getSn_CR(sn)) {
		;
	}
	if (socket(sn, Sn_MR_TCP, 80, 0x00) != sn) {
		printf("[PROV] HTTP socket open failed (SR=0x%02X)\r\n", getSn_SR(sn));
		return;
	}
	int8_t lret = listen(sn);
	uint8_t ip[4];
	getSIPR(ip);
	printf("[PROV] HTTP listen %u.%u.%u.%u:80 SR=0x%02X listen=%d\r\n",
	       ip[0], ip[1], ip[2], ip[3], getSn_SR(sn), (int)lret);
}

static void prov_init_http(void)
{
	if (s_http_inited) {
		return;
	}
	httpServer_init(s_http_tx, s_http_rx, 1, s_http_sock_list);
	reg_httpServer_webContent((uint8_t *)"index.html", (uint8_t *)PROV_INDEX_HTML);
	s_http_inited = true;
	printf("[PROV] HTTP server ready on socket %d (port 80)\r\n", SHELTER_HTTP_SOCKET_NUM);
}

bool Prov_IsActive(void)
{
	return s_prov_active;
}

void Prov_EnterSetupMode(void)
{
	if (s_prov_active) {
		return;
	}
	printf("\r\n[PROV] Entering direct-connect setup mode -> 192.168.0.100\r\n");
	W5500_SetProvisioningMode(true);
	W5500_StopDhcp();
	for (uint8_t sn = 0; sn < 8; sn++) {
		close(sn);
		while (getSn_CR(sn)) {
			;
		}
	}
	if (!W5500_ApplyProvisioningNetwork()) {
		printf("[PROV] Failed to apply provisioning network\r\n");
		return;
	}
	prov_init_http();
	prov_force_http_listen();
	for (int i = 0; i < 8; i++) {
		httpServer_run(0);
	}
	s_prov_active = true;
}

void Prov_RunHttpServerTick(void)
{
	if (!s_prov_active) {
		return;
	}
	httpServer_run(0);
}

void Prov_TimeTick1s(void)
{
	if (!s_prov_active) {
		return;
	}
	httpServer_time_handler();
}

void Prov_PrintHeartbeat(void)
{
	uint8_t ip[4];
	uint8_t mac[6];
	uint8_t sn = SHELTER_HTTP_SOCKET_NUM;

	getSIPR(ip);
	getSHAR(mac);
	printf("[PROV] alive link=%u IP=%u.%u.%u.%u MAC=%02X:%02X:%02X:%02X:%02X:%02X HTTP_SR=0x%02X (PC=192.168.0.10)\r\n",
	       (unsigned)(getPHYCFGR() & 0x01),
	       ip[0], ip[1], ip[2], ip[3],
	       mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
	       getSn_SR(sn));
}

uint8_t Prov_HandleStatusCgi(uint8_t *buf, uint32_t *file_len)
{
	wiz_NetInfo ni;
	wizchip_getnetinfo(&ni);
	int n = snprintf((char *)buf, 512,
		"<html><body style=\"font-family:sans-serif;background:#111;color:#eee;padding:16px\">"
		"<h3>현재 설정</h3><pre>"
		"Mode: %s\nIP: %u.%u.%u.%u\nBroker: %u.%u.%u.%u:%u\nUID pending save from flash</pre>"
		"<p><a href=\"/\">설정 페이지</a></p></body></html>",
		g_net_cfg.net_mode ? "DHCP" : "STATIC",
		ni.ip[0], ni.ip[1], ni.ip[2], ni.ip[3],
		g_net_cfg.broker_ip[0], g_net_cfg.broker_ip[1], g_net_cfg.broker_ip[2], g_net_cfg.broker_ip[3],
		g_net_cfg.broker_port);
	if (n < 0) {
		return HTTP_FAILED;
	}
	*file_len = (uint32_t)n;
	return HTTP_OK;
}

uint8_t Prov_HandleSaveCgi(st_http_request *req, uint8_t *buf, uint32_t *file_len)
{
	NetRuntimeConfig cfg = g_net_cfg;
	char *packet = (char *)req->URI;

	printf("[PROV] save.cgi POST received\r\n");
	uint8_t *mode_val = get_http_param_value(packet, "mode");
	bool is_dhcp = (mode_val && (strcmp((char *)mode_val, "dhcp") == 0));

	if (is_dhcp) {
		cfg.net_mode = 1;
	} else {
		cfg.net_mode = 0;
		if (!parse_ipv4_param(packet, "ip", cfg.ip)
				|| !parse_ipv4_param(packet, "sn", cfg.sn)
				|| !parse_ipv4_param(packet, "gw", cfg.gw)) {
			snprintf((char *)buf, 256, "IP/SN/GW 입력 오류");
			*file_len = strlen((char *)buf);
			return HTTP_OK;
		}
		if (!parse_ipv4_param(packet, "dns", cfg.dns)) {
			uint8_t dns_def[] = SHELTER_PROV_DNS;
			memcpy(cfg.dns, dns_def, 4);
		}
	}

	if (!parse_ipv4_param(packet, "broker", cfg.broker_ip)) {
		snprintf((char *)buf, 256, "브로커 IP 입력 오류");
		*file_len = strlen((char *)buf);
		return HTTP_OK;
	}

	uint8_t *port_val = get_http_param_value(packet, "port");
	if (port_val && port_val[0]) {
		int p = atoi((char *)port_val);
		if (p > 0 && p <= 65535) {
			cfg.broker_port = (uint16_t)p;
		}
	}
	if (cfg.broker_port == 0) {
		cfg.broker_port = SHELTER_MQTT_BROKER_PORT;
	}

	snprintf((char *)buf, 512,
		"<html><body style=\"font-family:sans-serif;background:#111;color:#eee;padding:20px\">"
		"<h3>설정 저장됨</h3>"
		"<p><b>1.</b> PC와의 직결 케이블을 분리하세요.<br>"
		"<b>2.</b> 보드를 현장 공유기/스위치 <b>LAN</b> 포트에 연결하세요.<br>"
		"<b>3.</b> 재부팅 후 DHCP로 IP를 받으면 CMS에서 ONLINE 확인.</p>"
		"<p style=\"color:#94a3b8;font-size:13px\">STATIC 저장 시 LAN에서 해당 IP로 접속합니다.</p>"
		"</body></html>");
	*file_len = strlen((char *)buf);

	if (cfg.net_mode) {
		printf("[PROV] Save: mode=DHCP broker=%u.%u.%u.%u:%u\r\n",
		       cfg.broker_ip[0], cfg.broker_ip[1], cfg.broker_ip[2], cfg.broker_ip[3],
		       cfg.broker_port);
	} else {
		printf("[PROV] Save: mode=STATIC ip=%u.%u.%u.%u broker=%u.%u.%u.%u:%u\r\n",
		       cfg.ip[0], cfg.ip[1], cfg.ip[2], cfg.ip[3],
		       cfg.broker_ip[0], cfg.broker_ip[1], cfg.broker_ip[2], cfg.broker_ip[3],
		       cfg.broker_port);
	}

	HAL_Delay(300);
	NetConfig_SaveAndApplyEx(&cfg, NET_SAVE_PROFILE);
	return HTTP_OK;
}