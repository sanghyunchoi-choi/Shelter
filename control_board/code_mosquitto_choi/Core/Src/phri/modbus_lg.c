#include "modbus_lg.h"
#include "config.h"
#include "app.h"
#include "crc.h"
#include <stdio.h>
#include <string.h>

/* ======================================================================
   [내부 상태 변수]
   ====================================================================== */
static MODBUS_SM _nextState    = MODBUS_SM_SLAVE_ID;
static uint8_t   _dataBuf[256];
static uint8_t   _dataIndex    = 0;
static uint8_t   _dataCountMax = 0;
static uint8_t   _cntInStat    = 0;
static uint8_t   _lastRecvBuf[256];
static uint8_t   _lastRecvLen  = 0;
static volatile uint8_t _received = 0;
#if SHELTER_RS485_DEBUG_LOG
static volatile uint32_t _ac_rx_isr_cnt = 0;
#endif
static uint8_t   _ac_rx_byte = 0;
static uint8_t   _ac_fast_poll = 0;

static void ModbusSmReset(void)
{
    _nextState     = MODBUS_SM_SLAVE_ID;
    _dataIndex     = 0;
    _dataCountMax  = 0;
    _cntInStat     = 0;
}

#if SHELTER_RS485_DEBUG_LOG
static void ModbusLogHex(const char *tag, const uint8_t *buf, uint16_t len)
{
    printf("%s (%u): ", tag, (unsigned)len);
    for (uint16_t i = 0; i < len; i++) {
        printf("%02X ", buf[i]);
    }
    printf("\r\n");
}
#endif

static void ModbusFlushRx(void)
{
    uint8_t  dummy;
    uint32_t t0 = HAL_GetTick();

    while ((HAL_GetTick() - t0) < 10U) {
        if (__HAL_UART_GET_FLAG(&huart4, UART_FLAG_RXNE)) {
            (void)HAL_UART_Receive(&huart4, &dummy, 1, 0);
        } else {
            break;
        }
    }
    __HAL_UART_CLEAR_OREFLAG(&huart4);
    __HAL_UART_CLEAR_NEFLAG(&huart4);
    __HAL_UART_CLEAR_FEFLAG(&huart4);
}

static void ModbusEnsureRxIT(void)
{
    if (huart4.RxState == HAL_UART_STATE_READY) {
        if (HAL_UART_Receive_IT(&huart4, &_ac_rx_byte, 1) != HAL_OK) {
#if SHELTER_RS485_DEBUG_LOG
            printf("[AC] RX IT start failed (RxState=%u)\r\n", (unsigned)huart4.RxState);
#endif
        }
    }
}

void Modbus_RestartRxIT(void)
{
    ModbusFlushRx();
    ModbusEnsureRxIT();
}

void Modbus_Init(void)
{
    ModbusSmReset();
    _received = 0;
    HAL_GPIO_WritePin(RTX_DIR4_GPIO_Port, RTX_DIR4_Pin, GPIO_PIN_RESET);
    Modbus_RestartRxIT();
#if SHELTER_RS485_DEBUG_LOG
    printf("[AC] Modbus init (UART4 9600)\r\n");
#endif
}

void Modbus_UART_ISR(UART_HandleTypeDef *huart)
{
    if (huart->Instance != UART4) {
        return;
    }

#if SHELTER_RS485_DEBUG_LOG
    _ac_rx_isr_cnt++;
#endif

    if (HAL_UART_Receive_IT(&huart4, &_ac_rx_byte, 1) != HAL_OK) {
#if SHELTER_RS485_DEBUG_LOG
        printf("[AC] RX IT restart failed in ISR\r\n");
#endif
    }

#if (SHELTER_RS485_DEBUG_LOG && SHELTER_RS485_LOG_EACH_BYTE)
    printf("[AC] rx %02X\r\n", _ac_rx_byte);
#endif

    ModbusSmInput(_ac_rx_byte);
}

/* ======================================================================
   [송신] RS485 방향 제어 포함 전송
   ====================================================================== */
static void ModbusRawSend(uint8_t *data, uint16_t len)
{
    ModbusFlushRx();
    HAL_GPIO_WritePin(RTX_DIR4_GPIO_Port, RTX_DIR4_Pin, GPIO_PIN_SET);
    HAL_UART_Transmit(&huart4, data, len, 100);
    /* ★ 2026-08-09 수정: 타임아웃 없는 busy-wait → 태스크 영구 정지 위험. 상한(50ms) 추가. */
    {
        uint32_t tc_start = HAL_GetTick();
        while (__HAL_UART_GET_FLAG(&huart4, UART_FLAG_TC) == RESET) {
            if ((HAL_GetTick() - tc_start) > 50) {
                printf("[LGAC] TX TC flag timeout — RS485 line fault?\r\n");
                break;
            }
        }
    }
    HAL_GPIO_WritePin(RTX_DIR4_GPIO_Port, RTX_DIR4_Pin, GPIO_PIN_RESET);
    osDelay(SHELTER_RS485_TURNAROUND_MS);
    /* TX 직후 Flush 금지 — 슬레이브 응답(수 ms 내)이 지워지는 현상 방지 */
    ModbusEnsureRxIT();
}

/* ======================================================================
   [수신] 상태 머신 인터럽트 입력
   ====================================================================== */
void ModbusSmInput(uint8_t input)
{
    if (_dataIndex >= sizeof(_dataBuf)) {
        ModbusSmReset();
    }
    _dataBuf[_dataIndex++] = input;

    switch (_nextState) {
        case MODBUS_SM_SLAVE_ID:
            if (input == MODBUS_SLAVE_ID) {
                _nextState = MODBUS_SM_FUNC_CODE;
            } else {
                _dataIndex = 0;
            }
            break;

        case MODBUS_SM_FUNC_CODE:
            _nextState = (input < 0x05) ? MODBUS_SM_BYTE_COUNT : MODBUS_SM_ADDRESS;
            break;

        case MODBUS_SM_BYTE_COUNT:
            _dataCountMax = input;
            _cntInStat    = 0;
            _nextState    = MODBUS_SM_DATA;
            break;

        case MODBUS_SM_ADDRESS:
            if (++_cntInStat == 2) {
                _dataCountMax = 2;
                _cntInStat    = 0;
                _nextState    = MODBUS_SM_DATA;
            }
            break;

        case MODBUS_SM_DATA:
            if (++_cntInStat == _dataCountMax) {
                _cntInStat = 0;
                _nextState = MODBUS_SM_CRC;
            }
            break;

        case MODBUS_SM_CRC:
            if (++_cntInStat == 2) {
                if (_dataIndex >= 4 && _dataBuf[0] == MODBUS_SLAVE_ID) {
                    uint16_t recv_crc = (uint16_t)_dataBuf[_dataIndex - 2]
                                      | ((uint16_t)_dataBuf[_dataIndex - 1] << 8);
                    uint16_t calc_crc = crc_modbus(_dataBuf, _dataIndex - 2);

                    if (recv_crc == calc_crc && !(_dataBuf[1] & 0x80)) {
                        memcpy(_lastRecvBuf, _dataBuf, _dataIndex);
                        _lastRecvLen = _dataIndex;
                        _received    = 1;
                    } else if (_dataBuf[1] & 0x80) {
#if SHELTER_RS485_DEBUG_LOG
                        printf("[AC] Modbus exception FC=0x%02X code=0x%02X\r\n",
                               _dataBuf[1], _dataBuf[2]);
#endif
                    } else {
#if SHELTER_RS485_DEBUG_LOG
                        ModbusLogHex("[AC] CRC fail frame", _dataBuf, _dataIndex);
                        printf("[AC] CRC calc=0x%04X recv=0x%04X\r\n", calc_crc, recv_crc);
#endif
                    }
                }
                ModbusSmReset();
            }
            break;
    }
}

/* ======================================================================
   [엔진] Modbus 패킷 조립 및 전송
   ====================================================================== */
int ModbusRequest(uint8_t func, uint16_t addr, uint16_t val)
{
    uint8_t  tx[8];
    uint16_t crc;
    int      i;

    tx[0] = MODBUS_SLAVE_ID;
    tx[1] = func;
    tx[2] = (uint8_t)(addr >> 8);
    tx[3] = (uint8_t)(addr & 0xFF);
    tx[4] = (uint8_t)(val >> 8);
    tx[5] = (uint8_t)(val & 0xFF);
    crc   = crc_modbus(tx, 6);
    tx[6] = (uint8_t)(crc & 0xFF);
    tx[7] = (uint8_t)(crc >> 8);

    ModbusSmReset();
    _received = 0;
#if SHELTER_RS485_DEBUG_LOG
    _ac_rx_isr_cnt = 0;
    ModbusLogHex("[AC] TX", tx, 8);
#endif
    ModbusRawSend(tx, 8);

    {
        int max_retries = _ac_fast_poll ? SHELTER_RS485_OFFLINE_RETRIES
                                        : SHELTER_RS485_ACK_RETRIES;
        for (i = 0; i < max_retries; i++) {
        osDelay(SHELTER_RS485_ACK_RETRY_MS);
        if (_received) {
#if SHELTER_RS485_DEBUG_LOG
            ModbusLogHex("[AC] RX OK", _lastRecvBuf, _lastRecvLen);
#endif
            return 0;
        }
        }
    }

#if SHELTER_RS485_DEBUG_LOG
    printf("[AC] ACK timeout FC=0x%02X addr=0x%04X val=0x%04X RxState=0x%02lX isr_bytes=%lu\r\n",
           func, addr, val, (unsigned long)huart4.RxState, (unsigned long)_ac_rx_isr_cnt);
#endif
    return -1;
}

void ModbusParse(uint16_t start_addr, uint8_t func)
{
    uint8_t *d = &_lastRecvBuf[3];

    if (func == FUNC_READ_COIL) {
        strcpy(dev_status.ac.pwr, (d[0] & 0x01) ? "ON" : "OFF");
    } else if (func == FUNC_READ_DISCRETE) {
        if (start_addr == ADDR_AC_CONN) {
            dev_status.ac.is_connected = (d[0] & 0x01) ? true : false;
        }
    } else if (func == FUNC_READ_INPUT) {
        if (start_addr == ADDR_INDOOR_TEMP) {
            dev_status.ac.temp_curr = (float)((int16_t)((d[0] << 8) | d[1])) / 10.0f;
        } else if (start_addr == ADDR_ERROR_CODE) {
            dev_status.ac.error_code = (uint16_t)((d[0] << 8) | d[1]);
        }
    } else if (func == FUNC_READ_HOLDING) {
        if (start_addr == ADDR_SET_MODE) {
            uint16_t m = (uint16_t)((d[0] << 8) | d[1]);
            const char *m_str[] = {"COOL", "DRY", "FAN", "AUTO", "HEAT"};
            if (m <= 4) {
                strcpy(dev_status.ac.mode, m_str[m]);
            }
            if (_lastRecvBuf[2] >= 4) {
                dev_status.ac.fan_speed = (uint16_t)((d[2] << 8) | d[3]);
            }
            if (_lastRecvBuf[2] >= 6) {
                dev_status.ac.temp_sel = (float)((d[4] << 8) | d[5]) / 10.0f;
            }
        } else if (start_addr == ADDR_SET_WIND) {
            dev_status.ac.fan_speed = (uint16_t)((d[0] << 8) | d[1]);
        } else if (start_addr == ADDR_SET_TEMP) {
            dev_status.ac.temp_sel = (float)((d[0] << 8) | d[1]) / 10.0f;
        }
    }
}

static void getACStatusLocked(int *ok_out, bool *discrete_ok, bool *discrete_conn)
{
    int  ok = 0;
    bool d_ok = false, d_conn = false;

    _ac_fast_poll = (!dev_status.ac.is_connected) ? 1 : 0;

    if (ModbusRequest(FUNC_READ_DISCRETE, ADDR_AC_CONN, 1) == 0) {
        ModbusParse(ADDR_AC_CONN, FUNC_READ_DISCRETE);
        d_ok   = true;
        d_conn = dev_status.ac.is_connected;
        ok++;
    }

    dev_status.ac.is_connected = (d_ok && d_conn);
    if (!dev_status.ac.is_connected) {
        _ac_fast_poll = 1;
        if (ok_out)        *ok_out = ok;
        if (discrete_ok)   *discrete_ok = d_ok;
        if (discrete_conn) *discrete_conn = d_conn;
        return;
    }

    _ac_fast_poll = 0;

    if (ModbusRequest(FUNC_READ_COIL, ADDR_AC_POWER, 1) == 0) {
        ModbusParse(ADDR_AC_POWER, FUNC_READ_COIL);
        ok++;
    }
    if (ModbusRequest(FUNC_READ_HOLDING, ADDR_SET_MODE, HOLDING_READ_COUNT) == 0) {
        ModbusParse(ADDR_SET_MODE, FUNC_READ_HOLDING);
        ok++;
    }
    if (ModbusRequest(FUNC_READ_INPUT, ADDR_ERROR_CODE, 1) == 0) {
        ModbusParse(ADDR_ERROR_CODE, FUNC_READ_INPUT);
        ok++;
    }
    if (ModbusRequest(FUNC_READ_INPUT, ADDR_INDOOR_TEMP, 1) == 0) {
        ModbusParse(ADDR_INDOOR_TEMP, FUNC_READ_INPUT);
        ok++;
    }

    if (ok_out)        *ok_out = ok;
    if (discrete_ok)   *discrete_ok = d_ok;
    if (discrete_conn) *discrete_conn = d_conn;
}

void getACStatus(void)
{
    if (osMutexAcquire(mqtt_mutex_id, 3000) != osOK) {
#if SHELTER_RS485_DEBUG_LOG
        printf("[AC] getACStatus mutex timeout (AP/tele may be waiting same mutex)\r\n");
#endif
        return;
    }
    getACStatusLocked(NULL, NULL, NULL);
    osMutexRelease(mqtt_mutex_id);
}

int setACPower(bool on)
{
    if (osMutexAcquire(mqtt_mutex_id, 200) != osOK) return -1;
    _ac_fast_poll = 0;
    int ret = ModbusRequest(FUNC_WRITE_COIL, ADDR_AC_POWER, on ? 0xFF00 : 0x0000);
    osMutexRelease(mqtt_mutex_id);
    return ret;
}

int setACMode(uint8_t mode)
{
    if (mode > AC_MODE_HEAT) {
        return -1;
    }
    if (osMutexAcquire(mqtt_mutex_id, 200) != osOK) return -1;
    _ac_fast_poll = 0;
    int ret = ModbusRequest(FUNC_WRITE_HOLDING, ADDR_SET_MODE, (uint16_t)mode);
    osMutexRelease(mqtt_mutex_id);
    return ret;
}

int setACTemperature(uint16_t temp_x10)
{
    /* LG 40003: 16.0~30.0 °C ×10 */
    if (temp_x10 < 160 || temp_x10 > 300) {
        return -1;
    }
    if (osMutexAcquire(mqtt_mutex_id, 200) != osOK) return -1;
    _ac_fast_poll = 0;
    int ret = ModbusRequest(FUNC_WRITE_HOLDING, ADDR_SET_TEMP, temp_x10);
    osMutexRelease(mqtt_mutex_id);
    return ret;
}

int setACWindSpeed(uint8_t speed)
{
    /* LG 40002: 1~6 (현장 실내기 1대) */
    if (speed < 1 || speed > 6) {
        return -1;
    }
    if (osMutexAcquire(mqtt_mutex_id, 200) != osOK) return -1;
    _ac_fast_poll = 0;
    int ret = ModbusRequest(FUNC_WRITE_HOLDING, ADDR_SET_WIND, (uint16_t)speed);
    osMutexRelease(mqtt_mutex_id);
    return ret;
}

int setACSpeed(uint8_t speed)
{
    return setACWindSpeed(speed);
}
