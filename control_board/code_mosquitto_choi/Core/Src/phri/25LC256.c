/**
 * @file 25LC256.c
 * @brief 25LC256-I/SN SPI EEPROM 드라이버 구현부 (프로덕션 릴리즈 버전)
 */
#include "25LC256.h"

static SPI_HandleTypeDef *EEPROM_SPI = NULL;
uint8_t EEPROM_StatusByte;
uint8_t RxBuffer[EEPROM_BUFFER_SIZE] = {0x00};

// OS 기동 전 하드웨어 타이머 정지를 대비한 마이크로초 레벨 로우레벨 딜레이
static void software_delay_us(uint32_t us) {
    volatile uint32_t count = us * 24; // 보드 클록에 맞춘 스핀 루프
    while(count--) {
        __NOP();
    }
}

/**
 * @brief  EEPROM 드라이버를 초기화하고, 칩 내부의 모든 하드웨어 쓰기 잠금(Block Protection)을 강제 해제합니다.
 */
void EEPROM_SPI_INIT(SPI_HandleTypeDef *hspi) {
    EEPROM_SPI = hspi;

    // 1. 상태 레지스터 수정을 위해 쓰기 허용 가동
    sEE_WriteEnable();
    software_delay_us(50);

    // 2. WRSR (Write Status Register, 0x01) 명령어 패킷 구성
    uint8_t status_cmd[2];
    status_cmd[0] = EEPROM_WRSR; // 상태 레지스터 쓰기 명령
    status_cmd[1] = 0x00;        // ★ 핵심: WPEN=0, BP1=0, BP0=0 (모든 메모리 영역 잠금 강제 해제!!)

    // 3. 단일 패킷으로 묶어서 상태 레지스터에 다이렉트 주입
    EEPROM_CS_LOW();
    HAL_SPI_Transmit(EEPROM_SPI, status_cmd, 2, 100);
    EEPROM_CS_HIGH();

    // 4. 상태 레지스터 반영 물리 대기
    EEPROM_SPI_WaitStandbyState();

    // 5. 쓰기 금지 리셋으로 초기화 마무리
    sEE_WriteDisable();
}


/**
 * @brief  EEPROM의 1개 페이지(64바이트 이내) 영역에 데이터를 기록합니다.
 */
EepromOperations EEPROM_SPI_WritePage(uint8_t* pBuffer, uint16_t WriteAddr, uint16_t NumByteToWrite) {
    if (EEPROM_SPI == NULL) return EEPROM_STATUS_ERROR;

    HAL_StatusTypeDef spiStatus;

    // 헤더(3바이트) + 최대 페이지 크기(64바이트)를 커버하는 단일 임시 패킷 버퍼 생성
    uint8_t tx_packet[3 + 64];

    // 안전 가드: 페이지 크기 초과 방지
    if (NumByteToWrite > 64) NumByteToWrite = 64;

    sEE_WriteEnable();

    // 단일 연속 메모리 공간에 명령어, 주소, 실제 데이터를 촘촘하게 패킹
    tx_packet[0] = EEPROM_WRITE;
    tx_packet[1] = (uint8_t)(WriteAddr >> 8);
    tx_packet[2] = (uint8_t)(WriteAddr);

    // 데이터 내용을 헤더 바로 뒤 바이트 공간에 밀착 복사
    memcpy(&tx_packet[3], pBuffer, NumByteToWrite);

    // 하드웨어 통신 시작 (CS Low)
    EEPROM_CS_LOW();

    // ★ [치명적인 버그 수정] 단 한 번의 락업 없는 다이렉트 송신으로 버스 끊김을 완벽 차단합니다.
    spiStatus = HAL_SPI_Transmit(EEPROM_SPI, tx_packet, 3 + NumByteToWrite, 300);

    // 하드웨어 통신 종료 (CS High)
    EEPROM_CS_HIGH();

    // EEPROM의 내부 하드웨어 물리 저장 대기 (WIP 폴링)
    if (EEPROM_SPI_WaitStandbyState() != 0) {
        return EEPROM_STATUS_ERROR;
    }

    sEE_WriteDisable();

    if (spiStatus != HAL_OK) return EEPROM_STATUS_ERROR;
    return EEPROM_STATUS_COMPLETE;
}


/**
 * @brief  페이지 경계를 자동 연산하여 임의 크기의 대량 데이터를 분할 기록합니다.
 */
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
            if (NumOfSingle != 0) {
                sEE_DataNum = NumOfSingle;
                pageWriteStatus = EEPROM_SPI_WritePage(pBuffer, WriteAddr, sEE_DataNum);
                if (pageWriteStatus != EEPROM_STATUS_COMPLETE) return pageWriteStatus;
            }
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

/**
 * @brief  임의 오프셋 주소로부터 정적 데이터를 읽어옵니다.
 */
EepromOperations EEPROM_SPI_ReadBuffer(uint8_t* pBuffer, uint16_t ReadAddr, uint16_t NumByteToRead) {
    if (EEPROM_SPI == NULL) return EEPROM_STATUS_ERROR;

    uint8_t header[3];

    header[0] = EEPROM_READ;
    header[1] = (uint8_t)(ReadAddr >> 8);
    header[2] = (uint8_t)(ReadAddr);

    EEPROM_CS_LOW();

    if (HAL_SPI_Transmit(EEPROM_SPI, header, 3, 100) != HAL_OK) {
        EEPROM_CS_HIGH();
        return EEPROM_STATUS_ERROR;
    }

    HAL_StatusTypeDef rxStatus = HAL_SPI_Receive(EEPROM_SPI, pBuffer, NumByteToRead, 500);

    EEPROM_CS_HIGH();

    if (rxStatus != HAL_OK) {
        return EEPROM_STATUS_ERROR;
    }
    return EEPROM_STATUS_COMPLETE;
}

uint8_t EEPROM_SendByte(uint8_t byte) {
    uint8_t answerByte = 0xFF;
    if (EEPROM_SPI == NULL) return 0xFF;

    if (HAL_SPI_TransmitReceive(EEPROM_SPI, &byte, &answerByte, 1, 200) != HAL_OK) {
        return 0xFF;
    }
    return answerByte;
}

void sEE_WriteEnable(void) {
    uint8_t command = EEPROM_WREN;
    EEPROM_CS_LOW();
    HAL_SPI_Transmit(EEPROM_SPI, &command, 1, 100);
    EEPROM_CS_HIGH();
}

void sEE_WriteDisable(void) {
    uint8_t command = EEPROM_WRDI;
    EEPROM_CS_LOW();
    HAL_SPI_Transmit(EEPROM_SPI, &command, 1, 100);
    EEPROM_CS_HIGH();
}

void sEE_WriteStatusRegister(uint8_t regval) {
    uint8_t command[2];
    command[0] = EEPROM_WRSR;
    command[1] = regval;

    sEE_WriteEnable();

    EEPROM_CS_LOW();
    HAL_SPI_Transmit(EEPROM_SPI, command, 2, 100);
    EEPROM_CS_HIGH();

    sEE_WriteDisable();
}

/**
 * @brief  WIP 플래그를 폴링하여 내부 물리 저장이 완료될 때까지 대기합니다.
 */
uint8_t EEPROM_SPI_WaitStandbyState(void) {
    if (EEPROM_SPI == NULL) return 1;

    uint8_t sEEstatus = 0x00;
    uint8_t command = EEPROM_RDSR;
    uint32_t safety_counter = 2000;

    do {
        EEPROM_CS_LOW();
        if (HAL_SPI_Transmit(EEPROM_SPI, &command, 1, 50) == HAL_OK) {
            HAL_SPI_Receive(EEPROM_SPI, &sEEstatus, 1, 50);
        }
        EEPROM_CS_HIGH();

        if (safety_counter-- == 0) {
            return 1;
        }

        software_delay_us(50);

    } while ((sEEstatus & EEPROM_WIP_FLAG) != 0);

    return 0;
}

void EEPROM_SPI_SendInstruction(uint8_t *instruction, uint8_t size) {
    if (EEPROM_SPI == NULL) return;
    HAL_SPI_Transmit(EEPROM_SPI, instruction, size, 200);
}
