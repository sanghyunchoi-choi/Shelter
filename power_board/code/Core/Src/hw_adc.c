#include "hw_adc.h"
#include <math.h>
#include <stdio.h>

/* DMA 버퍼 (32비트 정렬) */
__attribute__((aligned(32))) uint16_t g_adc_raw[ADC_CH_COUNT] = {0,};

/* 릴레이 OFF · 0A 시 센서 VOUT (ACS712 양방향 ≈ 2.5V @ 5V) */
float g_adc_offset[ADC_CH_COUNT] = {
    2.5f, 2.5f, 2.5f, 2.5f, 2.5f, 2.5f, 2.5f, 2.5f
};

/*
 * 릴레이 ON · 무부하 잔류 전류 보정 (mA)
 * ACS712-20A 실측값 × (100/185) 초기값 — 05B 교체 후 UART 로그로 재보정 권장
 */
const int16_t g_relay_noise_offset[ADC_CH_COUNT] = {
    205, 195, 140, 245, 200, 215, 180, 105
};

/**
 * @brief ADC raw -> 센서 VOUT (분압 역산)
 */
static float HW_ADC_Read_Sensor_V(uint8_t ch_idx)
{
    uint32_t sum = 0;

    /* DMA 스캔 1회분 이상 안정화를 위해 짧게 4회 평균 */
    for (int i = 0; i < 4; i++) {
        sum += g_adc_raw[ch_idx];
    }
    float avg_raw = (float)sum / 4.0f;
    float v_pin = (avg_raw * ADC_VREF) / ADC_RESOLUTION;
    return v_pin * VOLTAGE_DIVIDER;
}

void HW_ADC_Init(ADC_HandleTypeDef *hadc)
{
    HAL_ADC_Stop_DMA(hadc);
    HAL_ADCEx_Calibration_Start(hadc);
    HAL_Delay(100);

    if (HAL_ADC_Start_DMA(hadc, (uint32_t*)g_adc_raw, ADC_CH_COUNT) != HAL_OK) {
        printf("[ADC] DMA Start Error!\r\n");
    }

    __HAL_DMA_DISABLE_IT(hadc->DMA_Handle, DMA_IT_TC | DMA_IT_HT | DMA_IT_TE);
}

/**
 * @brief 0점 보정 — 릴레이 OFF, 0A (AC 영점 = VCC/2)
 */
void HW_ADC_Calibrate_Zero(void)
{
    printf("[ADC] AC zero cal (relay OFF, 0A)...\r\n");
    HAL_Delay(500);

    for (int ch = 0; ch < ADC_CH_COUNT; ch++) {
        float sum_v = 0.0f;
        for (int i = 0; i < AC_ZERO_AVG_COUNT; i++) {
            sum_v += HW_ADC_Read_Sensor_V((uint8_t)ch);
            HAL_Delay(1);
        }
        g_adc_offset[ch] = sum_v / (float)AC_ZERO_AVG_COUNT;
        printf(" Ch%d zero: %.3f V (pin %.3f V)\r\n", ch + 1,
               g_adc_offset[ch],
               g_adc_offset[ch] / VOLTAGE_DIVIDER);
    }
}

/**
 * @brief 220V AC 부하 전류 — RMS (양방향, 영점 기준 위·아래 파형)
 *
 * Irms = sqrt( mean( (V - Vzero)^2 ) ) / Sens
 * IP+/IP- 뒤집혀도 RMS 크기는 동일.
 */
int16_t HW_Get_Current_mA(uint8_t ch_idx, uint8_t is_relay_on)
{
    if (ch_idx >= ADC_CH_COUNT) {
        return 0;
    }
    if (is_relay_on == 0) {
        return 0; /* 로드 경로 OFF — 전류 0 보고 */
    }

    float sum_sq = 0.0f;
    const float v_zero = g_adc_offset[ch_idx];

    for (int i = 0; i < AC_RMS_SAMPLE_COUNT; i++) {
        float v = HW_ADC_Read_Sensor_V(ch_idx);
        float d = v - v_zero;
        sum_sq += d * d;
        if (i + 1 < AC_RMS_SAMPLE_COUNT) {
            HAL_Delay(AC_RMS_SAMPLE_PERIOD_MS);
        }
    }

    float rms_v = sqrtf(sum_sq / (float)AC_RMS_SAMPLE_COUNT);
    float current_ma = (rms_v * 1000.0f) / SENSOR_SENSITIVITY;

    /* 릴레이 ON 무부하 잔류 (기존 실측 mA 보정) */
    current_ma -= (float)g_relay_noise_offset[ch_idx];

    if (current_ma < 0.0f) {
        current_ma = 0.0f;
    }
    if (current_ma < (float)CURRENT_DEADBAND_MA) {
        current_ma = 0.0f;
    }

    if (current_ma > (float)SENSOR_MAX_CURRENT_MA) {
        current_ma = (float)SENSOR_MAX_CURRENT_MA;
    }

    return (int16_t)(current_ma + 0.5f);
}
