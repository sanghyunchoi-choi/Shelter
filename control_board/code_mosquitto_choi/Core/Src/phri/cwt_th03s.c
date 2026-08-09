#include "cwt_th03s.h"
#include <stdio.h>
#include <string.h>

/* [내부 전용] Modbus CRC16 계산 */
static uint16_t Modbus_CalculateCRC(uint8_t *data, uint16_t length) {
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x0001) { crc >>= 1; crc ^= 0xA001; }
            else { crc >>= 1; }
        }
    }
    return crc;
}

/* [내부 전용] RS485 방향 제어 (TX: SET, RX: RESET) */
static void RS485_SetMode(GPIO_PinState state) {
    HAL_GPIO_WritePin(RTX_DIR1_GPIO_Port, RTX_DIR1_Pin, state);
    for(volatile int i=0; i<1000; i++); // 라인 안정화
}

/**
 * [내부 전용] UART 수신 버퍼 완전히 비우기
 * 수신 레지스터(DR)에 데이터가 없을 때까지 모두 읽어내고, 오버런(ORE) 등 에러 플래그를 리셋합니다.
 */
static void RS485_ClearBuffer(void) {
    uint8_t dummy;
    // 1. RXNE(수신 데이터 있음) 플래그가 떠 있는 동안 계속 읽기
    while (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_RXNE)) {
        HAL_UART_Receive(&huart1, &dummy, 1, 0); // 타임아웃 0으로 즉시 읽기
    }
    // 2. Overrun Error 등 통신 에러 플래그 강제 클리어
    __HAL_UART_CLEAR_OREFLAG(&huart1);
    __HAL_UART_CLEAR_NEFLAG(&huart1);
    __HAL_UART_CLEAR_FEFLAG(&huart1);
}

/* [내부 전용] MCU UART 속도 재설정 */
static void MCU_UART_Reinit(uint32_t baudrate) {
    if (huart1.Init.BaudRate == baudrate) return;
    HAL_UART_DeInit(&huart1);
    huart1.Init.BaudRate = baudrate;
    HAL_UART_Init(&huart1);
    HAL_Delay(100);
}

/**
 * @brief 센서 초기화 (4800/9600 자동 스캔)
 */
void CWTTH03S_Modbus_Init(void) {
    THData dummy;
    //printf("[CWT] Checking Connection (Scan 4800/9600)...\r\n");

    MCU_UART_Reinit(4800);
    if (CWTTH03S_Modbus_ReadSensor(&dummy) == HAL_OK) {
        //printf("[CWT] Sensor Connected at 4800bps\r\n");
        return;
    }

    MCU_UART_Reinit(9600);
    if (CWTTH03S_Modbus_ReadSensor(&dummy) == HAL_OK) {
        //printf("[CWT] Sensor Connected at 9600bps\r\n");
        return;
    }
    //printf("[CWT] Error: No Response from Sensor!\r\n");
}

/**
 * @brief 온습도 수집 함수
 */
HAL_StatusTypeDef CWTTH03S_Modbus_ReadSensor(THData *data) {
    uint8_t req[8] = {0xFF, 0x03, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00};
    uint8_t res[9] = {0};
    uint16_t crc_calc;

    // 1. 요청 패킷 생성
    crc_calc = Modbus_CalculateCRC(req, 6);
    req[6] = crc_calc & 0xFF;
    req[7] = (crc_calc >> 8) & 0xFF;

    // 2. ★ 통신 시작 전 버퍼 비우기 실행 ★
    RS485_ClearBuffer();

    // 3. 송신 모드 및 데이터 전송
    RS485_SetMode(GPIO_PIN_SET);
    if (HAL_UART_Transmit(&huart1, req, 8, 100) != HAL_OK) {
        RS485_SetMode(GPIO_PIN_RESET);
        return HAL_ERROR;
    }

    // 4. 물리적 전송 완료 확인
    while(__HAL_UART_GET_FLAG(&huart1, UART_FLAG_TC) == RESET);

    // 5. 수신 전환 전 미세 딜레이 (센서 응답 준비 시간)
    osDelay(2);
    RS485_SetMode(GPIO_PIN_RESET);

    // 6. 응답 수신 (9바이트)
    if (HAL_UART_Receive(&huart1, res, 9, 500) != HAL_OK) {
        return HAL_TIMEOUT;
    }

    // 7. CRC 검증
    crc_calc = Modbus_CalculateCRC(res, 7);
    if (res[7] != (crc_calc & 0xFF) || res[8] != (crc_calc >> 8)) {
        return HAL_ERROR;
    }

    // 8. 데이터 파싱 (인덱스 주의: res[3]~[4] 습도, res[5]~[6] 온도)
    uint16_t humi_raw = (uint16_t)((res[3] << 8) | res[4]);
    int16_t temp_raw = (int16_t)((res[5] << 8) | res[6]);

    if (humi_raw == 0xFFFF || temp_raw == 0xFFFF) return HAL_ERROR;

    data->humi = (float)humi_raw / 10.0f;
    data->temp = (float)temp_raw / 10.0f;

    return HAL_OK;
}
