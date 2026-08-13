#include "HCSD.h"
#include <string.h>
//내부 온습도 센서
// 데이터 시트에 따른 명령 (HumiChip2 표준)
static uint8_t START_NOM[] = { 0x80 };

HCSD_HandleTypeDef hcsd_Init(I2C_HandleTypeDef* i2c_handle) {
    HCSD_HandleTypeDef hcsd_;
    hcsd_.i2c_handle = i2c_handle;
    hcsd_.device_address = I2C_HCSD_ADDR;
    hcsd_.last_status = 0;
    memset(hcsd_.data, 0, sizeof(hcsd_.data));
    return hcsd_;
}

/**
 * @brief I2C 통신을 통해 센서 데이터를 갱신합니다. (RTOS 대응 타임아웃 적용)
 */
uint8_t hcsd_UpdateValue(HCSD_HandleTypeDef *hcsd) {
    HAL_StatusTypeDef status;

    // 1. 측정 시작 명령 전송 (Wake-up)
    // HumiChip2는 주소만 보내거나 0x80을 보내 측정을 시작합니다.
    status = HAL_I2C_Master_Transmit(hcsd->i2c_handle, hcsd->device_address, START_NOM, 1, HCSD_I2C_TIMEOUT);
    if (status != HAL_OK) {
        hcsd->last_status = 1;
        return 1;
    }

    // 2. 측정 시간 대기 (RTOS용 Non-blocking 대기)
    // 센서가 데이터를 준비하는 데 약 30~40ms가 소요됩니다.
    // osDelay를 쓰거나, 로직상 다음 루프에서 읽는 것이 좋으나
    // 여기서는 간단히 HAL_Delay 대신 최소한의 Task Yield 효과를 위해 osDelay 사용을 권장합니다.
    osDelay(40);

    // 3. 데이터 4바이트 읽기
    status = HAL_I2C_Master_Receive(hcsd->i2c_handle, (hcsd->device_address | 0x01), hcsd->data, 4, HCSD_I2C_TIMEOUT);
    if (status != HAL_OK) {
        hcsd->last_status = 2;
        return 2;
    }

    hcsd->last_status = 0;
    return 0;
}

/**
 * @brief 갱신된 데이터를 바탕으로 온습도를 계산합니다.
 */
void hcsd_GetTemperatureAndHumidity(HCSD_HandleTypeDef *hcsd, float *temperature, float *humidity) {
    // 상태 비트 확인 (00: Normal, 01: Stale Data, 10: Command Mode)
    uint8_t status = (hcsd->data[0] >> 6);
//    printf("[HCSD_RAW] Status:%d | Raw: %02X %02X %02X %02X ",
//            status, hcsd->data[0], hcsd->data[1], hcsd->data[2], hcsd->data[3]);

    if (status == 0) { // 정상 데이터일 때만 계산
        uint16_t raw_humidity = ((hcsd->data[0] & 0x3F) << 8) | hcsd->data[1];
        uint16_t raw_temperature = (hcsd->data[2] << 6) | (hcsd->data[3] >> 2);

        *humidity = (float)raw_humidity / 16384.0f * 100.0f;
        *temperature = (float)raw_temperature / 16384.0f * 165.0f - 40.0f;
        //printf("-> OK [H:%.1f%%, T:%.1f'C]\r\n", *humidity, *temperature);
        hcsd->last_status = 0;
    } else {
        hcsd->last_status = 3; // Data Not Ready or Stale
        //const char* err_msg = (status == 1) ? "STALE" : (status == 2) ? "CMD_MODE" : "UNKNOWN";
        //printf("-> ERR [%s]\r\n", err_msg);
    }
}
