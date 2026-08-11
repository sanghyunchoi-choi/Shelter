/*
 * 25LC256.h
 *
 *  Created on: May 13, 2024
 *      Author: choi
 */

#ifndef INC_25LC256_H_
#define INC_25LC256_H_

/* C++ detection */
#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdbool.h>


/* M95040 SPI EEPROM defines */
#define EEPROM_WREN  0x06  /*!< Write Enable */
#define EEPROM_WRDI  0x04  /*!< Write Disable */
#define EEPROM_RDSR  0x05  /*!< Read Status Register */
#define EEPROM_WRSR  0x01  /*!< Write Status Register */
#define EEPROM_READ  0x03  /*!< Read from Memory Array */
#define EEPROM_WRITE 0x02  /*!< Write to Memory Array */

#define EEPROM_WIP_FLAG        0x01  /*!< Write In Progress (WIP) flag */

#define EEPROM_PAGESIZE        64    /*!< Pagesize according to documentation */
#define EEPROM_BUFFER_SIZE     64    /*!< EEPROM Buffer size. Setup to your needs */

#define EEPROM_CS_LOW()		HAL_GPIO_WritePin(EEP_CS_GPIO_Port, EEP_CS_Pin, GPIO_PIN_RESET);	// CS low
#define EEPROM_CS_HIGH()	HAL_GPIO_WritePin(EEP_CS_GPIO_Port, EEP_CS_Pin, GPIO_PIN_SET);		// CS high

/**
 * @brief EEPROM Operations statuses
 */
typedef enum {
	EEPROM_STATUS_PENDING,
	EEPROM_STATUS_COMPLETE,
	EEPROM_STATUS_ERROR
} EepromOperations;

void EEPROM_SPI_INIT(SPI_HandleTypeDef * hspi);
EepromOperations EEPROM_SPI_WriteBuffer(uint8_t* pBuffer, uint16_t WriteAddr, uint16_t NumByteToWrite);
EepromOperations EEPROM_SPI_WritePage(uint8_t* pBuffer, uint16_t WriteAddr, uint16_t NumByteToWrite);
EepromOperations EEPROM_SPI_ReadBuffer(uint8_t* pBuffer, uint16_t ReadAddr, uint16_t NumByteToRead);
uint8_t EEPROM_SPI_WaitStandbyState(void);

/* ★ 2026-08-10 신규: EEPROM이 실제로 응답하는지 30ms 안에 빠르게 확인.
   false면 net_config.c는 이후 모든 EEPROM I/O를 건너뛰고 config.h
   기본값으로 즉시 폴백해야 합니다 (MCU가 멈추는 일을 방지). */
bool EEPROM_SPI_Probe(void);

/* Low layer functions
   ★ 2026-08-10: 반환형을 EepromOperations로 변경 (기존 void → 에러 전파 가능하게).
   EEPROM_SendByte는 미사용이라 제거했습니다. */
EepromOperations sEE_WriteEnable(void);
void sEE_WriteDisable(void);
void sEE_WriteStatusRegister(uint8_t regval);
uint8_t sEE_ReadStatusRegister(void);

EepromOperations EEPROM_SPI_SendInstruction(uint8_t *instruction, uint8_t size);
void  EEPROM_SPI_ReadStatusByte(SPI_HandleTypeDef SPIe, uint8_t *statusByte );

#ifdef __cplusplus
}
#endif

#endif /* INC_25LC256_H_ */
