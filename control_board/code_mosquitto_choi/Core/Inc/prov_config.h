/**
 * @file prov_config.h
 * @brief PC 이더넷 직결 프로비저닝 (기본 IP 192.168.0.100, HTTP 설정 페이지)
 */
#ifndef INC_PROV_CONFIG_H_
#define INC_PROV_CONFIG_H_

#include <stdbool.h>
#include <stdint.h>

typedef struct _st_http_request st_http_request;

bool Prov_IsActive(void);
void Prov_EnterSetupMode(void);
void Prov_RunHttpServerTick(void);
void Prov_TimeTick1s(void);
void Prov_PrintHeartbeat(void);

uint8_t Prov_HandleSaveCgi(st_http_request *req, uint8_t *buf, uint32_t *file_len);
uint8_t Prov_HandleStatusCgi(uint8_t *buf, uint32_t *file_len);

#endif /* INC_PROV_CONFIG_H_ */
