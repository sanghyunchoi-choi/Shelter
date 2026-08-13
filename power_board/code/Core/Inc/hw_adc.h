/*
 * hw_adc.h
 *
 *  Created on: Mar 11, 2026
 *      Author: cshss
 */

#ifndef INC_HW_ADC_H_
#define INC_HW_ADC_H_

#include "main.h"

/* 하드웨어 상수 */
#define ADC_CH_COUNT            8
#define ADC_VREF                3.3f
#define ADC_RESOLUTION          4095.0f
#define VOLTAGE_DIVIDER         (37.0f / 27.0f)   /* MCU pin -> 센서 VOUT 역산 */
#define SENSOR_SENSITIVITY      0.185f            /* ACS712-05B @ 5V: 185 mV/A */
#define SENSOR_MAX_CURRENT_MA   5000              /* ACS712-05B 정격 ±5 A */

/* 220V AC RMS 측정 (50/60 Hz) */
#define AC_RMS_SAMPLE_COUNT     80    /* 샘플 수 */
#define AC_RMS_SAMPLE_PERIOD_MS 1     /* 80ms 윈도 ≈ 4주기@50Hz */
#define AC_ZERO_AVG_COUNT       100   /* 영점 보정 평균 횟수 */

/* 릴레이 ON · 무부하 시 잔류 (mA) — hw_adc.c 배열과 동기 (현장 재보정 권장) */
#define CURRENT_DEADBAND_MA     40

extern uint16_t g_adc_raw[ADC_CH_COUNT];
extern const int16_t g_relay_noise_offset[ADC_CH_COUNT];

void    HW_ADC_Init(ADC_HandleTypeDef *hadc);
void    HW_ADC_Calibrate_Zero(void);
int16_t HW_Get_Current_mA(uint8_t ch_idx, uint8_t is_relay_on);

#endif /* INC_HW_ADC_H_ */
