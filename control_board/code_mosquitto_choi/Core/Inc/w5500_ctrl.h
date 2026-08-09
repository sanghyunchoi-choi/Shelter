#ifndef INC_W5500_CTRL_H_
#define INC_W5500_CTRL_H_

#include "main.h"
#include "config.h"
#include "wizchip_conf.h"
#include <stddef.h>
#include <stdbool.h>

void W5500_Select(void);
void W5500_Deselect(void);
uint8_t W5500_ReadByte(void);
void W5500_WriteByte(uint8_t wb);
void W5500_ReadBurst(uint8_t* pBuf, uint16_t len);
void W5500_WriteBurst(uint8_t* pBuf, uint16_t len);

void W5500_Init(void);
void W5500_Interrupt_Config(void);

/** Static 또는 DHCP(config.h) 적용. 성공 시 true */
bool W5500_ApplyNetwork(void);

/** 케이블 연결 + 유효 IPv4 여부 */
bool W5500_NetworkReady(uint8_t *ip_out4);

void W5500_PrintNetwork(const char *tag);

/** STM32 96-bit UID → 24자리 HEX */
void Board_GetDeviceUuid(char *out, size_t out_len);

#if SHELTER_NET_USE_DHCP
/** 1초 주기 태스크에서 호출 (DHCP lease 유지) */
void W5500_DhcpTick(void);
#endif

#endif /* INC_W5500_CTRL_H_ */
