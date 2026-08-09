/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32h7xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "cmsis_os2.h"
#include "app.h"
/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define RTX_DIR4_Pin GPIO_PIN_0
#define RTX_DIR4_GPIO_Port GPIOC
#define RTX_DIR2_Pin GPIO_PIN_1
#define RTX_DIR2_GPIO_Port GPIOC
#define WIZ_RST_Pin GPIO_PIN_3
#define WIZ_RST_GPIO_Port GPIOC
#define WIZ_CS_Pin GPIO_PIN_4
#define WIZ_CS_GPIO_Port GPIOA
#define WIZ_SCK_Pin GPIO_PIN_5
#define WIZ_SCK_GPIO_Port GPIOA
#define WIZ_MISO_Pin GPIO_PIN_6
#define WIZ_MISO_GPIO_Port GPIOA
#define WIZ_MOSI_Pin GPIO_PIN_7
#define WIZ_MOSI_GPIO_Port GPIOA
#define WIZ_INT_Pin GPIO_PIN_4
#define WIZ_INT_GPIO_Port GPIOC
#define rly_1_Pin GPIO_PIN_5
#define rly_1_GPIO_Port GPIOC
#define rly_2_Pin GPIO_PIN_0
#define rly_2_GPIO_Port GPIOB
#define rly_3_Pin GPIO_PIN_1
#define rly_3_GPIO_Port GPIOB
#define rly_4_Pin GPIO_PIN_2
#define rly_4_GPIO_Port GPIOB
#define rly_5_Pin GPIO_PIN_11
#define rly_5_GPIO_Port GPIOF
#define rly_6_Pin GPIO_PIN_12
#define rly_6_GPIO_Port GPIOF
#define rly_7_Pin GPIO_PIN_13
#define rly_7_GPIO_Port GPIOF
#define rly_8_Pin GPIO_PIN_14
#define rly_8_GPIO_Port GPIOF
#define rly_9_Pin GPIO_PIN_15
#define rly_9_GPIO_Port GPIOF
#define rly_10_Pin GPIO_PIN_0
#define rly_10_GPIO_Port GPIOG
#define rly_11_Pin GPIO_PIN_1
#define rly_11_GPIO_Port GPIOG
#define rly_12_Pin GPIO_PIN_7
#define rly_12_GPIO_Port GPIOE
#define rly_13_Pin GPIO_PIN_8
#define rly_13_GPIO_Port GPIOE
#define rly_14_Pin GPIO_PIN_9
#define rly_14_GPIO_Port GPIOE
#define rly_15_Pin GPIO_PIN_10
#define rly_15_GPIO_Port GPIOE
#define RTX_DIR5_Pin GPIO_PIN_14
#define RTX_DIR5_GPIO_Port GPIOB
#define RTX_DIR3_Pin GPIO_PIN_15
#define RTX_DIR3_GPIO_Port GPIOB
#define RTX_DIR1_Pin GPIO_PIN_11
#define RTX_DIR1_GPIO_Port GPIOA
#define EEP_CS_Pin GPIO_PIN_11
#define EEP_CS_GPIO_Port GPIOG
#define EEP_MISO_Pin GPIO_PIN_12
#define EEP_MISO_GPIO_Port GPIOG
#define EEP_SCK_Pin GPIO_PIN_13
#define EEP_SCK_GPIO_Port GPIOG
#define EEP_MOSI_Pin GPIO_PIN_14
#define EEP_MOSI_GPIO_Port GPIOG
#define PM_RESET_Pin GPIO_PIN_8
#define PM_RESET_GPIO_Port GPIOB
#define PM_SET_Pin GPIO_PIN_9
#define PM_SET_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */
extern osThreadId_t StartMQTTHandle;
extern const osThreadAttr_t StartMQTT_attributes;
extern osThreadId_t StartAppSensorHandle;
extern const osThreadAttr_t StartAppSensor_attributes;
extern osThreadId_t StartAppTimeHandle;
extern const osThreadAttr_t StartAppTime_attributes;
extern osThreadId_t PowerBoardHandle;
extern const osThreadAttr_t PowerBoard_attributes;
extern osThreadId_t APHandle;
extern const osThreadAttr_t AP_attributes;
extern osThreadId_t ACHandle;
extern const osThreadAttr_t AC_attributes;
extern osThreadId_t FANHandle;
extern const osThreadAttr_t FAN_attributes;

extern osMutexId_t mqtt_mutexHandle;
extern const osMutexAttr_t mqtt_mutex_attributes;

extern SPI_HandleTypeDef hspi1;
extern I2C_HandleTypeDef hi2c1;
extern I2C_HandleTypeDef hi2c2;
extern I2C_HandleTypeDef hi2c3;


extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;
extern UART_HandleTypeDef huart3;
extern UART_HandleTypeDef huart4;
extern UART_HandleTypeDef huart5;
extern UART_HandleTypeDef huart7;
extern UART_HandleTypeDef huart8;

extern TIM_HandleTypeDef htim1;
/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
