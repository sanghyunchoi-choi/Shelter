/*
 * Power_board.h
 *
 *  Created on: Mar 4, 2026
 *      Author: cshss
 */

#ifndef SRC_PHRI_POWER_BOARD_H_
#define SRC_PHRI_POWER_BOARD_H_

#include "main.h"
#include "app.h" // DeviceStatus 및 PowerData 구조체 참조

#define PWR_MAX_CHANNELS MAX_POWER_CHANNELS
#define PWR_RES_LEN      30  // Get All 응답 패킷 길이 (30바이트)

/* --- 함수 선언 --- */

void PowerBoard_Init(void);

/**
 * @brief 특정 보드의 8개 채널 일괄 제어
 * @param board_id: 대상 보드 ID
 * @param states: 각 채널의 ON(1)/OFF(0) 상태 배열
 */
HAL_StatusTypeDef PowerBoard_ControlAll(uint8_t board_id, uint8_t *states);

/**
 * @brief 특정 보드의 개별 채널 제어
 */
HAL_StatusTypeDef PowerBoard_ControlSingle(uint8_t board_id, uint8_t channel, uint8_t state);

/**
 * @brief 특정 보드로부터 전체 채널 데이터를 읽어와 갱신
 * @param board_id: 대상 보드 ID
 * @param pwr_array: 데이터를 저장할 구조체 배열 포인터
 */
HAL_StatusTypeDef PowerBoard_UpdateAllData(PowerData *pwr_array);

uint8_t PowerBoard_CalculateBCC(uint8_t *data, uint8_t len);

#endif /* SRC_PHRI_POWER_BOARD_H_ */
