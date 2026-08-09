#include "updm010ub.h" // 헤더 파일명 확인
#include <string.h>
#include <stdio.h>
//먼지 센서


static uint8_t  g_dust_rx_byte;
static uint8_t  g_dust_packet[DUST_MAX_BUF_LEN]; // 32바이트 전체 저장
static uint8_t  g_dust_idx = 0;
volatile static uint8_t g_data_ready = 0;
static DustData g_parsed_data = {0, 0, 0};
static UART_HandleTypeDef *g_huart_ptr = NULL;

uint8_t DUST_Init(UART_HandleTypeDef *huart) {
    if (huart == NULL) return 0;
    g_huart_ptr = huart;
    g_dust_idx = 0;
    g_data_ready = 0;
    memset(g_dust_packet, 0, sizeof(g_dust_packet));
    HAL_UART_Receive_IT(g_huart_ptr, &g_dust_rx_byte, 1);
    return 1;
}

/* 체크섬 검증: Byte00 ~ Byte29까지 모두 더함 */
static uint8_t DUST_Verify_CS(void) {
    uint16_t sum = 0;
    for (int i = 0; i < 30; i++) {
        sum += g_dust_packet[i];
    }
    uint16_t received_cs = (g_dust_packet[IDX_VERIFY_H] << 8) | g_dust_packet[IDX_VERIFY_L];
    return (sum == received_cs) ? 1 : 0;
}

uint8_t DUST_GetReadyData(DustData *outData) {
    if (g_data_ready) {
        g_data_ready = 0;
        *outData = g_parsed_data;
        return 1;
    }
    return 0;
}

void DUST_UART_ISR(UART_HandleTypeDef *huart) {
    if (g_huart_ptr == NULL || huart->Instance != g_huart_ptr->Instance) return;

    /* 단계별 패킷 수집 로직 */
    if (g_dust_idx == 0) {
        if (g_dust_rx_byte == DUST_HEADER0) {
            g_dust_packet[g_dust_idx++] = g_dust_rx_byte; // [0]번에 0x42 저장
        }
    }
    else if (g_dust_idx == 1) {
        if (g_dust_rx_byte == DUST_HEADER1) {
            g_dust_packet[g_dust_idx++] = g_dust_rx_byte; // [1]번에 0x4D 저장
        } else {
            g_dust_idx = 0; // 헤더가 틀리면 다시 처음부터
        }
    }
    else {
        // [2]번부터 [31]번까지 순서대로 저장
        g_dust_packet[g_dust_idx++] = g_dust_rx_byte;

        if (g_dust_idx >= DUST_MAX_BUF_LEN) {
            if (DUST_Verify_CS()) {
                /* 이제 인덱스가 데이터시트 번호와 완벽히 일치합니다! */
                g_parsed_data.pm1_0 = (g_dust_packet[IDX_PM1_H] << 8) | g_dust_packet[IDX_PM1_L];
                g_parsed_data.pm2_5 = (g_dust_packet[IDX_PM25_H] << 8) | g_dust_packet[IDX_PM25_L];
                g_parsed_data.pm10  = (g_dust_packet[IDX_PM10_H] << 8) | g_dust_packet[IDX_PM10_L];
                g_data_ready = 1;
            } else {
                // 체크섬 틀렸을 때 디버깅용 카운트 등을 넣으면 좋음
                printf("[ERR] Dust Checksum Mismatch!\n");
            }
            g_dust_idx = 0; // 한 패킷 끝났으니 초기화
        }
    }
    HAL_UART_Receive_IT(g_huart_ptr, &g_dust_rx_byte, 1);
}
