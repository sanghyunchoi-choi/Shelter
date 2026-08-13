/*
 * 25LC256.c
 *
 *  Created on: May 13, 2024
 *      Author: choi
 *
 * ============================================================================
 * ★★★ 2026-08-10 전면 재검토 ★★★
 * 이 파일에는 타임아웃이 없는 busy-wait가 6곳 있었고, 통신 실패 시
 * Error_Handler()를 호출하는 곳이 3곳 있었습니다. 이 프로젝트의
 * Error_Handler()는 다음과 같습니다 (Core/Src/main.c):
 *     __disable_irq();
 *     while (1) {}
 * 즉 EEPROM이 배선되어 있지 않거나 응답하지 않으면(현장에서 실제로
 * 발생한 상황) MCU 전체가 인터럽트까지 꺼진 채로 영구 정지합니다.
 * MQTT는 물론 아무것도 동작하지 않게 됩니다 — 이번에 NetConfig_Load()/
 * NetConfig_CheckRollback()을 켜면 접속이 안 되던 원인이 바로 이것입니다.
 *
 * 이번 수정 원칙:
 *   1) 모든 대기 루프에 HAL_GetTick() 기준의 확정적 타임아웃을 둡니다.
 *   2) Error_Handler()를 절대 호출하지 않고, 항상 에러 코드를 반환합니다.
 *   3) 타임아웃/에러 발생 시 EEPROM_SPI->State를 강제로 READY로 되돌려
 *      다음 호출이 곧바로 또 멈추지 않도록 합니다(HAL_SPI_Abort 시도 후
 *      실패해도 State를 직접 복구).
 *   4) EEPROM_SPI_Probe()를 신규 추가 — 아주 짧은 타임아웃(30ms)으로
 *      EEPROM 존재 여부만 빠르게 확인해, 없으면 이후 모든 읽기/쓰기를
 *      net_config.c 쪽에서 건너뛸 수 있게 합니다.
 * ============================================================================
 */

#include "25LC256.h"

SPI_HandleTypeDef *EEPROM_SPI;

uint8_t EEPROM_StatusByte;
uint8_t RxBuffer[EEPROM_BUFFER_SIZE] = {0x00};

/* 단일 SPI 트랜잭션(HAL 호출 1건)에 허용하는 최대 대기 시간.
   개별 HAL_SPI_Transmit/Receive 호출 자체의 timeout 인자와는 별개로,
   "State != READY"류 대기 루프에 적용하는 상한입니다. */
#define EEPROM_OP_TIMEOUT_MS   100U

void EEPROM_SPI_INIT(SPI_HandleTypeDef * hspi) {
    EEPROM_SPI = hspi;
}

/**
 * @brief 통신 실패/타임아웃 이후 SPI 핸들 상태를 안전하게 복구합니다.
 *        Error_Handler()로 MCU를 멈추는 대신, 다음 호출이 즉시 재시도할 수
 *        있도록 상태만 정리합니다.
 */
static void EEPROM_SPI_RecoverState(void) {
    if (EEPROM_SPI == NULL) return;
    HAL_SPI_Abort(EEPROM_SPI);          /* 진행 중인 트랜잭션 강제 중단 시도 */
    EEPROM_SPI->State = HAL_SPI_STATE_READY;  /* Abort가 실패해도 강제로 READY 복구 */
    EEPROM_CS_HIGH();
}

/**
 * @brief EEPROM이 실제로 응답하는지 아주 짧게(30ms) 확인합니다.
 *        net_config.c는 이 함수가 실패하면 이후 모든 EEPROM I/O를 건너뛰고
 *        즉시 config.h 기본값으로 폴백해야 합니다.
 * @return true = 응답함(정상), false = 응답 없음/타임아웃
 */
bool EEPROM_SPI_Probe(void) {
    if (EEPROM_SPI == NULL) return false;

    uint8_t command[1] = { EEPROM_RDSR };
    uint8_t status[1] = { 0 };

    uint32_t start = HAL_GetTick();
    while (EEPROM_SPI->State != HAL_SPI_STATE_READY) {
        if ((HAL_GetTick() - start) > 30) { EEPROM_SPI_RecoverState(); return false; }
    }

    EEPROM_CS_LOW();
    HAL_StatusTypeDef st1 = HAL_SPI_Transmit(EEPROM_SPI, command, 1, 30);
    HAL_StatusTypeDef st2 = HAL_SPI_Receive(EEPROM_SPI, status, 1, 30);
    EEPROM_CS_HIGH();

    if (st1 != HAL_OK || st2 != HAL_OK) {
        EEPROM_SPI_RecoverState();
        return false;
    }
    return true;
}

EepromOperations EEPROM_SPI_WritePage(uint8_t* pBuffer, uint16_t WriteAddr, uint16_t NumByteToWrite) {
	uint32_t t0 = HAL_GetTick();
	while (EEPROM_SPI->State != HAL_SPI_STATE_READY) {
		if ((HAL_GetTick() - t0) > EEPROM_OP_TIMEOUT_MS) { EEPROM_SPI_RecoverState(); return EEPROM_STATUS_ERROR; }
	}

	HAL_StatusTypeDef spiTransmitStatus = HAL_ERROR;

	if (sEE_WriteEnable() != EEPROM_STATUS_COMPLETE) {
		return EEPROM_STATUS_ERROR;
	}

	uint8_t header[3];
	header[0] = EEPROM_WRITE;
	header[1] = WriteAddr >> 8;
	header[2] = WriteAddr;

	EEPROM_CS_LOW();

	if (EEPROM_SPI_SendInstruction((uint8_t*)header, 3) != EEPROM_STATUS_COMPLETE) {
		EEPROM_CS_HIGH();
		return EEPROM_STATUS_ERROR;
	}

	for (uint8_t i = 0; i < 5; i++) {
		spiTransmitStatus = HAL_SPI_Transmit(EEPROM_SPI, pBuffer, NumByteToWrite, 100);
		if (spiTransmitStatus == HAL_BUSY) {
			HAL_Delay(5);
		} else {
			break;
		}
	}

	EEPROM_CS_HIGH();

	if (spiTransmitStatus != HAL_OK) {
		EEPROM_SPI_RecoverState();
		return EEPROM_STATUS_ERROR;
	}

	if (EEPROM_SPI_WaitStandbyState() != 0) {
		return EEPROM_STATUS_ERROR;
	}

	sEE_WriteDisable();

	return EEPROM_STATUS_COMPLETE;
}

EepromOperations EEPROM_SPI_WriteBuffer(uint8_t* pBuffer, uint16_t WriteAddr, uint16_t NumByteToWrite) {
	uint16_t NumOfPage = 0, NumOfSingle = 0, Addr = 0, count = 0, temp = 0;
	uint16_t sEE_DataNum = 0;

	EepromOperations pageWriteStatus = EEPROM_STATUS_PENDING;

	Addr = WriteAddr % EEPROM_PAGESIZE;
	count = EEPROM_PAGESIZE - Addr;
	NumOfPage =  NumByteToWrite / EEPROM_PAGESIZE;
	NumOfSingle = NumByteToWrite % EEPROM_PAGESIZE;

	if (Addr == 0) {
		if (NumOfPage == 0) {
			sEE_DataNum = NumByteToWrite;
			pageWriteStatus = EEPROM_SPI_WritePage(pBuffer, WriteAddr, sEE_DataNum);
			if (pageWriteStatus != EEPROM_STATUS_COMPLETE) return pageWriteStatus;
		} else {
			while (NumOfPage--) {
				sEE_DataNum = EEPROM_PAGESIZE;
				pageWriteStatus = EEPROM_SPI_WritePage(pBuffer, WriteAddr, sEE_DataNum);
				if (pageWriteStatus != EEPROM_STATUS_COMPLETE) return pageWriteStatus;
				WriteAddr +=  EEPROM_PAGESIZE;
				pBuffer += EEPROM_PAGESIZE;
			}
			sEE_DataNum = NumOfSingle;
			pageWriteStatus = EEPROM_SPI_WritePage(pBuffer, WriteAddr, sEE_DataNum);
			if (pageWriteStatus != EEPROM_STATUS_COMPLETE) return pageWriteStatus;
		}
	} else {
		if (NumOfPage == 0) {
			if (NumOfSingle > count) {
				temp = NumOfSingle - count;
				sEE_DataNum = count;
				pageWriteStatus = EEPROM_SPI_WritePage(pBuffer, WriteAddr, sEE_DataNum);
				if (pageWriteStatus != EEPROM_STATUS_COMPLETE) return pageWriteStatus;
				WriteAddr +=  count;
				pBuffer += count;
				sEE_DataNum = temp;
				pageWriteStatus = EEPROM_SPI_WritePage(pBuffer, WriteAddr, sEE_DataNum);
			} else {
				sEE_DataNum = NumByteToWrite;
				pageWriteStatus = EEPROM_SPI_WritePage(pBuffer, WriteAddr, sEE_DataNum);
			}
			if (pageWriteStatus != EEPROM_STATUS_COMPLETE) return pageWriteStatus;
		} else {
			NumByteToWrite -= count;
			NumOfPage =  NumByteToWrite / EEPROM_PAGESIZE;
			NumOfSingle = NumByteToWrite % EEPROM_PAGESIZE;
			sEE_DataNum = count;
			pageWriteStatus = EEPROM_SPI_WritePage(pBuffer, WriteAddr, sEE_DataNum);
			if (pageWriteStatus != EEPROM_STATUS_COMPLETE) return pageWriteStatus;
			WriteAddr +=  count;
			pBuffer += count;

			while (NumOfPage--) {
				sEE_DataNum = EEPROM_PAGESIZE;
				pageWriteStatus = EEPROM_SPI_WritePage(pBuffer, WriteAddr, sEE_DataNum);
				if (pageWriteStatus != EEPROM_STATUS_COMPLETE) return pageWriteStatus;
				WriteAddr +=  EEPROM_PAGESIZE;
				pBuffer += EEPROM_PAGESIZE;
			}

			if (NumOfSingle != 0) {
				sEE_DataNum = NumOfSingle;
				pageWriteStatus = EEPROM_SPI_WritePage(pBuffer, WriteAddr, sEE_DataNum);
				if (pageWriteStatus != EEPROM_STATUS_COMPLETE) return pageWriteStatus;
			}
		}
	}

	return EEPROM_STATUS_COMPLETE;
}

EepromOperations EEPROM_SPI_ReadBuffer(uint8_t* pBuffer, uint16_t ReadAddr, uint16_t NumByteToRead) {
	uint32_t t0 = HAL_GetTick();
	while (EEPROM_SPI->State != HAL_SPI_STATE_READY) {
		if ((HAL_GetTick() - t0) > EEPROM_OP_TIMEOUT_MS) { EEPROM_SPI_RecoverState(); return EEPROM_STATUS_ERROR; }
	}

	uint8_t header[3];
	header[0] = EEPROM_READ;
	header[1] = ReadAddr >> 8;
	header[2] = ReadAddr;

	EEPROM_CS_LOW();

	if (EEPROM_SPI_SendInstruction(header, 3) != EEPROM_STATUS_COMPLETE) {
		EEPROM_CS_HIGH();
		return EEPROM_STATUS_ERROR;
	}

	HAL_StatusTypeDef rx_st;
	uint32_t t1 = HAL_GetTick();
	do {
		rx_st = HAL_SPI_Receive(EEPROM_SPI, (uint8_t*)pBuffer, NumByteToRead, 200);
		if (rx_st == HAL_BUSY) {
			if ((HAL_GetTick() - t1) > EEPROM_OP_TIMEOUT_MS) {
				EEPROM_CS_HIGH();
				EEPROM_SPI_RecoverState();
				return EEPROM_STATUS_ERROR;
			}
			HAL_Delay(1);
		}
	} while (rx_st == HAL_BUSY);

	EEPROM_CS_HIGH();

	if (rx_st != HAL_OK) {
		EEPROM_SPI_RecoverState();
		return EEPROM_STATUS_ERROR;
	}

	return EEPROM_STATUS_COMPLETE;
}

/**
 * @brief  Enables the write access to the EEPROM.
 * @retval EEPROM_STATUS_COMPLETE / EEPROM_STATUS_ERROR
 */
EepromOperations sEE_WriteEnable(void) {
	EEPROM_CS_LOW();
	uint8_t command[1] = { EEPROM_WREN };
	EepromOperations st = EEPROM_SPI_SendInstruction((uint8_t*)command, 1);
	EEPROM_CS_HIGH();
	return st;
}

/**
 * @brief  Disables the write access to the EEPROM.
 */
void sEE_WriteDisable(void) {
	EEPROM_CS_LOW();
	uint8_t command[1] = { EEPROM_WRDI };
	EEPROM_SPI_SendInstruction((uint8_t*)command, 1);
	EEPROM_CS_HIGH();
}

/**
 * @brief  Polls the WIP flag until write completes, with a hard timeout.
 * @retval 0 = 정상 완료, 1 = 실패/타임아웃
 */
uint8_t EEPROM_SPI_WaitStandbyState(void) {
	uint8_t sEEstatus[1] = { 0x00 };
	uint8_t command[1] = { EEPROM_RDSR };

	EEPROM_CS_LOW();

	if (EEPROM_SPI_SendInstruction((uint8_t*)command, 1) != EEPROM_STATUS_COMPLETE) {
		EEPROM_CS_HIGH();
		return 1;
	}

	uint32_t wait_start = HAL_GetTick();
	do {
		HAL_StatusTypeDef rx_st;
		do {
			rx_st = HAL_SPI_Receive(EEPROM_SPI, (uint8_t*)sEEstatus, 1, 200);
			if (rx_st == HAL_BUSY) {
				if ((HAL_GetTick() - wait_start) > 200) {
					EEPROM_CS_HIGH();
					EEPROM_SPI_RecoverState();
					return 1;
				}
				HAL_Delay(1);
			}
		} while (rx_st == HAL_BUSY);

		if (rx_st != HAL_OK) {
			EEPROM_CS_HIGH();
			EEPROM_SPI_RecoverState();
			return 1;
		}

		if ((HAL_GetTick() - wait_start) > 200) {
			EEPROM_CS_HIGH();
			return 1;
		}
		HAL_Delay(1);
	} while ((sEEstatus[0] & EEPROM_WIP_FLAG) == SET);

	EEPROM_CS_HIGH();
	return 0;
}

/**
 * @brief Low level function to send header data to EEPROM.
 * @retval EEPROM_STATUS_COMPLETE / EEPROM_STATUS_ERROR
 */
EepromOperations EEPROM_SPI_SendInstruction(uint8_t *instruction, uint8_t size) {
	uint32_t t0 = HAL_GetTick();
	while (EEPROM_SPI->State == HAL_SPI_STATE_RESET) {
		if ((HAL_GetTick() - t0) > EEPROM_OP_TIMEOUT_MS) { EEPROM_SPI_RecoverState(); return EEPROM_STATUS_ERROR; }
	}

	if (HAL_SPI_Transmit(EEPROM_SPI, (uint8_t*)instruction, (uint16_t)size, 200) != HAL_OK) {
		EEPROM_SPI_RecoverState();
		return EEPROM_STATUS_ERROR;
	}
	return EEPROM_STATUS_COMPLETE;
}
