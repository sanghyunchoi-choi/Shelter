/*
 * HCSD.h
 *
 *  Created on: May 29, 2024
 *      Author: choi
 */

#ifndef INC_HCSD_H_
#define INC_HCSD_H_
#include "main.h"


#define I2C_HCSD_ADDR          (0x28 << 1)
#define HCSD_I2C_TIMEOUT       100  // RTOS용 적정 타임아웃 (ms)

typedef struct {
    I2C_HandleTypeDef* i2c_handle;
    uint8_t device_address;
    uint8_t data[4];
    uint8_t last_status; // 0: OK, 1: Transmit Fail, 2: Receive Fail
} HCSD_HandleTypeDef;

/* 함수 원형 */
HCSD_HandleTypeDef hcsd_Init(I2C_HandleTypeDef* i2c_handle);
uint8_t hcsd_UpdateValue(HCSD_HandleTypeDef *hcsd); // ReadValue를 Update 개념으로 변경
void hcsd_GetTemperatureAndHumidity(HCSD_HandleTypeDef *hcsd, float *temperature, float *humidity);

#endif /* INC_HCSD_H_ */
