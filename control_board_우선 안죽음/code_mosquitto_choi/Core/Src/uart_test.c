/*
 * uart_test.c
 *
 *  Created on: Mar 4, 2026
 *      Author: cshss
 */
#include "uart_test.h"
#include <string.h>

void uart_test(void)
{
	char msg[128];

	// ---------------------------------------------------------
	// [UART 1] RS-485 (RTX_DIR1_Pin 사용)
	// ---------------------------------------------------------
	snprintf(msg, sizeof(msg), "[UART1] RS-485 Test Message\r\n");
	HAL_GPIO_WritePin(RTX_DIR1_GPIO_Port, RTX_DIR1_Pin, GPIO_PIN_SET); // 송신 모드
	if (HAL_UART_Transmit(&huart1, (uint8_t*)msg, strlen(msg), 100) == HAL_OK) {
		printf("UART 1: Send OK (RS-485)\r\n");
	}
	HAL_GPIO_WritePin(RTX_DIR1_GPIO_Port, RTX_DIR1_Pin, GPIO_PIN_RESET); // 수신 모드 복구
	HAL_Delay(100);

	// ---------------------------------------------------------
	// [UART 2] RS-485 (RTX_DIR2_Pin 사용)
	// ---------------------------------------------------------
	snprintf(msg, sizeof(msg), "[UART2] RS-485 Test Message\r\n");
	HAL_GPIO_WritePin(RTX_DIR2_GPIO_Port, RTX_DIR2_Pin, GPIO_PIN_SET);
	if (HAL_UART_Transmit(&huart2, (uint8_t*)msg, strlen(msg), 100) == HAL_OK) {
		printf("UART 2: Send OK (RS-485)\r\n");
	}
	HAL_GPIO_WritePin(RTX_DIR2_GPIO_Port, RTX_DIR2_Pin, GPIO_PIN_RESET);
	HAL_Delay(100);

	// ---------------------------------------------------------
	// [UART 3] RS-485 (RTX_DIR3_Pin 사용)
	// ---------------------------------------------------------
	snprintf(msg, sizeof(msg), "[UART3] RS-485 Test Message\r\n");
	HAL_GPIO_WritePin(RTX_DIR3_GPIO_Port, RTX_DIR3_Pin, GPIO_PIN_SET);
	if (HAL_UART_Transmit(&huart3, (uint8_t*)msg, strlen(msg), 100) == HAL_OK) {
		printf("UART 3: Send OK (RS-485)\r\n");
	}
	HAL_GPIO_WritePin(RTX_DIR3_GPIO_Port, RTX_DIR3_Pin, GPIO_PIN_RESET);
	HAL_Delay(100);

	// ---------------------------------------------------------
	// [UART 4] RS-485 (RTX_DIR4_Pin 사용)
	// ---------------------------------------------------------
	snprintf(msg, sizeof(msg), "[UART4] RS-485 Test Message\r\n");
	HAL_GPIO_WritePin(RTX_DIR4_GPIO_Port, RTX_DIR4_Pin, GPIO_PIN_SET);
	if (HAL_UART_Transmit(&huart4, (uint8_t*)msg, strlen(msg), 100) == HAL_OK) {
		printf("UART 4: Send OK (RS-485)\r\n");
	}
	HAL_GPIO_WritePin(RTX_DIR4_GPIO_Port, RTX_DIR4_Pin, GPIO_PIN_RESET);
	HAL_Delay(100);

	// ---------------------------------------------------------
	// [UART 5] RS-485 (RTX_DIR5_Pin 사용 - 분전반용)
	// ---------------------------------------------------------
	snprintf(msg, sizeof(msg), "[UART5] RS-485 PDU Test Message\r\n");
	HAL_GPIO_WritePin(RTX_DIR5_GPIO_Port, RTX_DIR5_Pin, GPIO_PIN_SET);
	if (HAL_UART_Transmit(&huart5, (uint8_t*)msg, strlen(msg), 100) == HAL_OK) {
		printf("UART 5: Send OK (RS-485 PDU)\r\n");
	}
	HAL_GPIO_WritePin(RTX_DIR5_GPIO_Port, RTX_DIR5_Pin, GPIO_PIN_RESET);

	printf("\r\n--- RS-485 All Port TX Test Done ---\r\n");

}
