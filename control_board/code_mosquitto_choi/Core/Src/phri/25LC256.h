/**
 * @file 25LC256.h
 * @brief 25LC256-I/SN SPI EEPROM 드라이버 헤더
 */
#ifndef INC_25LC256_H_
#define INC_25LC256_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* 25LC256 SPI EEPROM 명령어 정의 */
#define EEPROM_WREN            0x06  /*!< Write Enable (쓰기 허용) */
#define EEPROM_WRDI            0x04  /*!< Write Disable (쓰기 금지) */
#define EEPROM_RDSR            0x05  /*!< Read Status Register (상태 읽기) */
#define EEPROM_WRSR            0x01  /*!< Write Status Register (상태 쓰기) */
#define EEPROM_READ            0x03  /*!< Read from Memory Array (데이터 읽기) */
#define EEPROM_WRITE           0x02  /*!< Write to Memory Array (데이터 쓰기) */

#define EEPROM_WIP_FLAG        0x01  /*!< Write In Progress (WIP) 비트 플래그 */
#define EEPROM_PAGESIZE        64    /*!< 25LC256 하드웨어 페이지 크기 (64바이트) */
#define EEPROM_BUFFER_SIZE     64

/* 소프트웨어 CS(NSS) 제어 매크로 (CubeMX에서 생성된 핀 이름 매칭 필수) */
/* 25LC256.h 매크로 안전성 교정본 */
#define EEPROM_CS_LOW()   do { HAL_GPIO_WritePin(EEP_CS_GPIO_Port, EEP_CS_Pin, GPIO_PIN_RESET); } while(0)
#define EEPROM_CS_HIGH()  do { HAL_GPIO_WritePin(EEP_CS_GPIO_Port, EEP_CS_Pin, GPIO_PIN_SET); } while(0)


/**
 * @brief EEPROM 동작 처리 상태 반환 구조체
 */
typedef enum {
    EEPROM_STATUS_PENDING,
    EEPROM_STATUS_COMPLETE,
    EEPROM_STATUS_ERROR
} EepromOperations;

/* 전역 함수 프로토타입 정의 */
void EEPROM_SPI_INIT(SPI_HandleTypeDef *hspi);
EepromOperations EEPROM_SPI_ReadBuffer(uint8_t* pBuffer, uint16_t ReadAddr, uint16_t NumByteToRead);
EepromOperations EEPROM_SPI_WriteBuffer(uint8_t* pBuffer, uint16_t WriteAddr, uint16_t NumByteToWrite);
EepromOperations EEPROM_SPI_WritePage(uint8_t* pBuffer, uint16_t WriteAddr, uint16_t NumByteToWrite);
uint8_t EEPROM_SPI_WaitStandbyState(void);

/* 하위 레이어 로우레벨 제어 함수 */
uint8_t EEPROM_SendByte(uint8_t byte);
void sEE_WriteEnable(void);
void sEE_WriteDisable(void);
void sEE_WriteStatusRegister(uint8_t regval);
void EEPROM_SPI_SendInstruction(uint8_t *instruction, uint8_t size);

#ifdef __cplusplus
}
#endif

#endif /* INC_25LC256_H_ */
