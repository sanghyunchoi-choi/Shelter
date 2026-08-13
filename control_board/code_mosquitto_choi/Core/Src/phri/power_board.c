#include "power_board.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

extern UART_HandleTypeDef huart5;

static osMutexId_t s_pb_uart_mutex;

static bool PB_UartLock(uint32_t timeout_ms)
{
    if (s_pb_uart_mutex == NULL) {
        return true;
    }
    return (osMutexAcquire(s_pb_uart_mutex, timeout_ms) == osOK);
}

static void PB_UartUnlock(void)
{
    if (s_pb_uart_mutex != NULL) {
        osMutexRelease(s_pb_uart_mutex);
    }
}

/**
 * @brief RS485 방향 제어 (High: 송신, Low: 수신)
 */
static void RS485_PWR_SetMode(GPIO_PinState state) {
    HAL_GPIO_WritePin(RTX_DIR5_GPIO_Port, RTX_DIR5_Pin, state);
}

/**
 * @brief BCC(XOR) 계산 함수
 */
uint8_t PowerBoard_CalculateBCC(uint8_t *data, uint8_t len) {
    uint8_t bcc = 0;
    for (uint8_t i = 0; i < len; i++) {
        bcc ^= data[i];
    }
    return bcc;
}

/**
 * @brief 파워보드 초기화 (통합 구조체 dev_status 참조)
 */
void PowerBoard_Init(void) {
    if (s_pb_uart_mutex == NULL) {
        s_pb_uart_mutex = osMutexNew(NULL);
    }
    RS485_PWR_SetMode(GPIO_PIN_RESET); // 기본 수신 대기

    // 기본값 설정 (ID 구분 없이 초기화가 필요한 경우 사용)
    for (int i = 0; i < MAX_POWER_CHANNELS; i++) {
        dev_status.pwr_ch[i].ch = i + 1;
        dev_status.pwr_ch[i].volt = 220;
        dev_status.pwr_ch[i].curr = 0.0f;
        dev_status.pwr_ch[i].watt = 0.0f;
        dev_status.pwr_ch[i].b_id = 0; /* RS485 수신 전 placeholder — PB 패킷에서 실측 ID로 갱신 */
        strcpy(dev_status.pwr_ch[i].pwr, "OFF");
    }
}

/**
 * @brief RS485 패킷에서 동적으로 보드 ID를 추출하여 구조체 내부 멤버에 직접 저장하는 함수
 */
HAL_StatusTypeDef PowerBoard_UpdateAllData(PowerData *pwr_array) {
    uint8_t rx_buf[PWR_RES_LEN] = {0};
    uint8_t head = 0;
    uint32_t start_tick = HAL_GetTick();

    if (!PB_UartLock(200)) {
        return HAL_TIMEOUT;
    }

    // 1. 수신 모드 고정 및 쓰레기 FIFO 청소
    RS485_PWR_SetMode(GPIO_PIN_RESET);
    {
        /* ★ 2026-08-09: 극단적 노이즈 상황 대비 상한 추가 (정상 시 몇 바이트 내 종료) */
        int guard = 0;
        while (__HAL_UART_GET_FLAG(&huart5, UART_FLAG_RXNE) && guard < 64) {
            volatile uint32_t dummy = huart5.Instance->RDR;
            (void)dummy;
            guard++;
        }
    }

    // 2. STX(0x02) 검색 (1.2초 타임아웃)
    while (1) {
        if (HAL_UART_Receive(&huart5, &head, 1, 10) == HAL_OK && head == 0x02) {
            break;
        }
        if (HAL_GetTick() - start_tick > 1200) {
            PB_UartUnlock();
            return HAL_TIMEOUT;
        }
    }

    // 3. 나머지 29바이트 수신
    rx_buf[0] = 0x02;
    if (HAL_UART_Receive(&huart5, &rx_buf[1], PWR_RES_LEN - 1, 100) != HAL_OK) {
        PB_UartUnlock();
        return HAL_TIMEOUT;
    }

    // 4. 패킷 유효성 검증 (ETX, BCC 체크)
    if (rx_buf[28] != 0x03) {
        PB_UartUnlock();
        return HAL_ERROR;
    }
    if (rx_buf[29] != PowerBoard_CalculateBCC(rx_buf, 29)) {
        PB_UartUnlock();
        return HAL_ERROR;
    }

    // 5. 데이터 파싱 및 구조체 직접 매핑
    // 💡 고정 ID를 검사하지 않고, 수신 패킷 1번지의 실제 ID를 구조체 멤버에 바로 주입합니다.
    for (int i = 0; i < MAX_POWER_CHANNELS; i++) {
        int base = 4 + (i * 3);

        // ★ 핵심: 구조체 내부 b_id 멤버에 실제 하드웨어 실측 ID 반영!
        pwr_array[i].b_id = rx_buf[1];

        strcpy(pwr_array[i].pwr, (rx_buf[base] & 0x01) ? "ON" : "OFF");

        uint16_t raw_ma = (uint16_t)((rx_buf[base + 1] << 8) | rx_buf[base + 2]);
        pwr_array[i].curr = (float)raw_ma / 1000.0f;
        pwr_array[i].volt = 220; // 구조체 내부 int 타입 일치화
        pwr_array[i].watt = (float)pwr_array[i].volt * pwr_array[i].curr;
        pwr_array[i].ch = i + 1;
    }

    PB_UartUnlock();
    return HAL_OK;
}

/**
 * @brief 개별 채널 제어 (14바이트 패킷 송신)
 * @param states: 8개 채널의 On(1)/Off(0) 상태 배열
 */
HAL_StatusTypeDef PowerBoard_ControlAll(uint8_t board_id, uint8_t *states) {
    uint8_t packet[14];

    if (!PB_UartLock(500)) {
        return HAL_TIMEOUT;
    }

    packet[0] = 0x02;     // STX
    packet[1] = board_id; // ID
    packet[2] = 0x0E;     // Length
    packet[3] = 0xAA;     // Command

    for (int i = 0; i < MAX_POWER_CHANNELS; i++) {
        packet[4 + i] = ((i + 1) << 4) | (states[i] & 0x01);
    }

    packet[12] = 0x03;    // ETX
    packet[13] = PowerBoard_CalculateBCC(packet, 13);

    RS485_PWR_SetMode(GPIO_PIN_SET);

    // 💡 [교정] MCU를 통째로 멈추는 HAL_Delay 대신
    // 하드웨어 핀 스위칭을 위한 미세 루프 딜레이(약 수십 마이크로초)로 가볍게 변경합니다.
    for(volatile int delay_gap = 0; delay_gap < 500; delay_gap++);

    HAL_StatusTypeDef status = HAL_UART_Transmit(&huart5, packet, 14, 100);
    /* ★ 2026-08-09 수정: 타임아웃 없는 busy-wait → 태스크 영구 정지 위험. 상한(50ms) 추가. */
    {
        uint32_t tc_start = HAL_GetTick();
        while (__HAL_UART_GET_FLAG(&huart5, UART_FLAG_TC) == RESET) {
            if ((HAL_GetTick() - tc_start) > 50) {
                printf("[PWR] TX TC flag timeout — RS485 line fault?\r\n");
                break;
            }
        }
    }

    for(volatile int delay_gap = 0; delay_gap < 500; delay_gap++);
    RS485_PWR_SetMode(GPIO_PIN_RESET);

    PB_UartUnlock();
    return status;
}

/**
 * @brief 개별 채널 제어 (기존 ControlAll 안전 재활용)
 * @param channel: 제어할 채널 번호 (1~8)
 * @param state: On(1)/Off(0)
 */
HAL_StatusTypeDef PowerBoard_ControlSingle(uint8_t board_id, uint8_t channel, uint8_t state) {
    uint8_t states[MAX_POWER_CHANNELS];

    // [버그 수정 완료] 타 채널들이 강제로 꺼지지 않도록
    // 실제 장치에서 읽혀온 하드웨어 실시간 캐시 데이터를 기준으로 개별 비트 마스크 보정 실행
    for (int i = 0; i < MAX_POWER_CHANNELS; i++) {
        states[i] = (strcmp(dev_status.pwr_ch[i].pwr, "ON") == 0) ? 1 : 0;
    }

    // 타겟 채널 단추만 목적 비트(state)로 정밀 스위칭
    if (channel >= 1 && channel <= MAX_POWER_CHANNELS) {
        states[channel - 1] = (state & 0x01);
    }

    return PowerBoard_ControlAll(board_id, states);
}
