#include "himpel.h"
#include "config.h"
#include "app.h"
#include <stdio.h>
#include <string.h>

/* ======================================================================
   [1. 내부 상태 관리 변수]
   ====================================================================== */
static HIMPEL_SM        _nextState  = HIMPEL_SM_STX;
static uint8_t          _dataBuf[RX_BUFFER_SIZE];
static uint8_t          _dataIndex  = 0;
static uint8_t          _recvBuf[HIMPEL_PACKET_SIZE];
static volatile uint8_t _received   = 0;
#if SHELTER_RS485_DEBUG_LOG
static volatile uint32_t _ap_rx_isr_cnt = 0;
#endif
static uint8_t          _ap_rx_byte = 0;
static osMutexId_t      ap_bus_mutex = NULL;

#if SHELTER_RS485_DEBUG_LOG
static void HimpelLogHex(const char *tag, const uint8_t *buf, uint16_t len)
{
    printf("%s (%u): ", tag, (unsigned)len);
    for (uint16_t i = 0; i < len; i++) {
        printf("%02X ", buf[i]);
    }
    printf("\r\n");
}
#endif

static void HimpelFlushRx(void)
{
    uint8_t  dummy;
    uint32_t t0 = HAL_GetTick();

    while ((HAL_GetTick() - t0) < 10U) {
        if (__HAL_UART_GET_FLAG(&huart3, UART_FLAG_RXNE)) {
            (void)HAL_UART_Receive(&huart3, &dummy, 1, 0);
        } else {
            break;
        }
    }
    __HAL_UART_CLEAR_OREFLAG(&huart3);
    __HAL_UART_CLEAR_NEFLAG(&huart3);
    __HAL_UART_CLEAR_FEFLAG(&huart3);
}

static void HimpelEnsureRxIT(void)
{
    if (huart3.RxState == HAL_UART_STATE_READY) {
        if (HAL_UART_Receive_IT(&huart3, &_ap_rx_byte, 1) != HAL_OK) {
#if SHELTER_RS485_DEBUG_LOG
            printf("[AP] RX IT start failed (RxState=%u)\r\n", (unsigned)huart3.RxState);
#endif
        }
    }
}

/* ======================================================================
   [2. 초기화 및 상태 머신 리셋]
   ====================================================================== */
void Himpel_RestartRxIT(void)
{
    HimpelFlushRx();
    HimpelEnsureRxIT();
}

void Himpel_Init(void)
{
    if (ap_bus_mutex == NULL) {
        const osMutexAttr_t attr = { .name = "ap_bus" };
        ap_bus_mutex = osMutexNew(&attr);
    }
    HimpelSmReset();
    HAL_GPIO_WritePin(RTX_DIR3_GPIO_Port, RTX_DIR3_Pin, GPIO_PIN_RESET);
    Himpel_RestartRxIT();
#if SHELTER_RS485_DEBUG_LOG
    printf("[AP] Himpel init (USART3 9600)\r\n");
#endif
}

void Himpel_UART_ISR(UART_HandleTypeDef *huart)
{
    uint8_t byte;

    if (huart->Instance != USART3) {
        return;
    }

    byte = _ap_rx_byte;
#if SHELTER_RS485_DEBUG_LOG
    _ap_rx_isr_cnt++;
#endif

    if (HAL_UART_Receive_IT(&huart3, &_ap_rx_byte, 1) != HAL_OK) {
#if SHELTER_RS485_DEBUG_LOG
        printf("[AP] RX IT restart failed in ISR\r\n");
#endif
    }

#if (SHELTER_RS485_DEBUG_LOG && SHELTER_RS485_LOG_EACH_BYTE)
    printf("[AP] rx %02X\r\n", byte);
#endif

    HimpelSmInput(byte);
}

void HimpelSmReset(void)
{
    _nextState = HIMPEL_SM_STX;
    _dataIndex = 0;
    memset(_dataBuf, 0x00, sizeof(_dataBuf));
}

/* ======================================================================
   [3. 수신 패킷 → dev_status 매핑]
   ★ 수정: Mutex 대기를 osWaitForever → 200ms 타임아웃으로 변경.
     osWaitForever는 다른 태스크가 Mutex를 해제하지 않을 경우
     해당 태스크가 영구 정지(데드락)될 수 있습니다.
     200ms 내에 획득 실패 시 패킷을 버리고 다음 수신을 기다립니다.
   ====================================================================== */
void HimpelUpdateStatus(const uint8_t* packet)
{
    if (packet == NULL) {
        return;
    }
    if (packet[0] != HIMPEL_STX || packet[1] != HIMPEL_HEADER) {
#if SHELTER_RS485_DEBUG_LOG
        printf("[AP] Invalid packet header (STX/HD)\r\n");
#endif
        return;
    }

    HimpelStatusMessage msg;
    memcpy(&msg, &packet[HIMPEL_PKT_DATA_OFF], sizeof(HimpelStatusMessage));

    if (osMutexAcquire(mqtt_mutex_id, 200) != osOK) return; // 타임아웃 시 패킷 폐기

    /* 1. 전원 및 모드 */
    strcpy(dev_status.ap.pwr, msg.ubPower ? "ON" : "OFF");

    if      (msg.ubAutomatic)   strcpy(dev_status.ap.mode, "AUTO");
    else if (msg.ubAirClean)    strcpy(dev_status.ap.mode, "CLEAN");
    else if (msg.ubVentilation) strcpy(dev_status.ap.mode, "VENT");
    else if (msg.ubBypass)      strcpy(dev_status.ap.mode, "BYPASS");
    else if (msg.ubModeHeater)  strcpy(dev_status.ap.mode, "HEATER");
    else                        strcpy(dev_status.ap.mode, "NONE");

    /* 2. 풍량 (0~4) */
    if      (msg.ubFanOff)   dev_status.ap.fan_speed = 0;
    else if (msg.ubFanLow)   dev_status.ap.fan_speed = 1;
    else if (msg.ubFanMid)   dev_status.ap.fan_speed = 2;
    else if (msg.ubFanHigh)  dev_status.ap.fan_speed = 3;
    else if (msg.ubFanTurbo) dev_status.ap.fan_speed = 4;

    dev_status.ap.uv_on = (bool)msg.ubUV;

    /* 3. 공기질 센서 (High/Low Byte 결합) */
    dev_status.ap.co2        = (msg.u8CO2_H  << 8) | msg.u8CO2_L;
    dev_status.ap.dust_pm25  = (msg.u8PM25_H << 8) | msg.u8PM25_L;
    dev_status.ap.dust_pm10  = (msg.u8PM10_H << 8) | msg.u8PM10_L;
    dev_status.ap.dust_pm1_0 = (msg.u8PM1_0_H<< 8) | msg.u8PM1_0_L;
    dev_status.ap.tvoc       = (msg.u8TVOC_H << 8) | msg.u8TVOC_L;

    /* 4. 온습도 (Sign 1bit + Value 7bit) */
    dev_status.ap.temp_in  = msg.inTempSign  ? -(float)msg.inTempVal  : (float)msg.inTempVal;
    dev_status.ap.temp_out = msg.outTempSign ? -(float)msg.outTempVal : (float)msg.outTempVal;
    dev_status.ap.humi     = msg.inHumiVal;

    /* 5. 장치 관리 */
    dev_status.ap.is_connected = true;
    dev_status.ap.error_code   = ((uint8_t*)&msg)[3];
    /* Himpel 패킷에 필터 잔량 % 없음 — 알람/센서 이상 시 0, 정상 시 100 */
    dev_status.ap.filter_life  = (msg.ubDustErr || msg.ubCO2Err || msg.ubCtrlErr) ? 0 : 100;

    osMutexRelease(mqtt_mutex_id);
}

/* ======================================================================
   [4. 통신 레이어] RS485 패킷 송수신
   ★ Mutex는 호출자(APTask, Setter API)가 관리.
     HimpelSendData 자체는 UART 송수신과 FRC 검증만 담당.
     is_connected 갱신은 HimpelUpdateStatus(Mutex 내부) 또는
     실패 경로(아래)에서 처리합니다.
   ====================================================================== */
int HimpelSendData(HIMPEL_ID da, HIMPEL_CMD cmd, const HimpelControlMessage* msg)
{
    uint8_t sendBuf[HIMPEL_PACKET_SIZE] = {0};
    uint8_t frc = 0x00;
    int     ret = -1;

    if (ap_bus_mutex == NULL || osMutexAcquire(ap_bus_mutex, 500) != osOK)
        return -1;

    sendBuf[0] = HIMPEL_STX;
    sendBuf[1] = HIMPEL_HEADER;
    sendBuf[2] = ID_CONTROL;
    sendBuf[3] = (uint8_t)da;
    sendBuf[4] = (uint8_t)cmd;

    if (cmd == CMD_CONTROL && msg != NULL)
        memcpy(&sendBuf[5], msg, sizeof(HimpelControlMessage));
    else
        memset(&sendBuf[5], 0x00, HIMPEL_DATA_SIZE);

    /* FRC 계산 (XOR: [1] ~ [PacketSize-3]) */
    for (int i = 1; i < HIMPEL_PACKET_SIZE - 2; i++) frc ^= sendBuf[i];
    sendBuf[22] = frc;
    sendBuf[23] = HIMPEL_EOP;

    HimpelSmReset();
    _received = 0;
#if SHELTER_RS485_DEBUG_LOG
    _ap_rx_isr_cnt = 0;
    HimpelLogHex("[AP] TX", sendBuf, HIMPEL_PACKET_SIZE);
#endif

    HimpelFlushRx();
    HAL_GPIO_WritePin(RTX_DIR3_GPIO_Port, RTX_DIR3_Pin, GPIO_PIN_SET);
    HAL_UART_Transmit(&huart3, sendBuf, HIMPEL_PACKET_SIZE, 100);
    /* ★ 2026-08-09 수정: 타임아웃 없는 busy-wait → 태스크 영구 정지 위험. 상한(50ms) 추가. */
    {
        uint32_t tc_start = HAL_GetTick();
        while (__HAL_UART_GET_FLAG(&huart3, UART_FLAG_TC) == RESET) {
            if ((HAL_GetTick() - tc_start) > 50) {
                printf("[AP] TX TC flag timeout — RS485 line fault?\r\n");
                break;
            }
        }
    }
    HAL_GPIO_WritePin(RTX_DIR3_GPIO_Port, RTX_DIR3_Pin, GPIO_PIN_RESET);
    osDelay(SHELTER_RS485_TURNAROUND_MS);
    HimpelEnsureRxIT();

    int max_retry = RETRY_COUNT;
    if (cmd == CMD_REQ_STATUS && !dev_status.ap.is_connected) {
        max_retry = SHELTER_RS485_OFFLINE_RETRIES;
    }

    for (int i = 0; i < max_retry; i++) {
        osDelay(SHELTER_RS485_ACK_RETRY_MS);
        if (_received) {
            break;
        }
    }

    if (_received) {
        uint8_t c_frc = 0x00;
        for (int i = 1; i < HIMPEL_PACKET_SIZE - 2; i++) {
            c_frc ^= _recvBuf[i];
        }
        if (c_frc == _recvBuf[22]) {
#if SHELTER_RS485_DEBUG_LOG
            HimpelLogHex("[AP] RX OK", _recvBuf, HIMPEL_PACKET_SIZE);
#endif
            HimpelUpdateStatus(_recvBuf);
            ret = 0;
            goto done;
        }
#if SHELTER_RS485_DEBUG_LOG
        HimpelLogHex("[AP] FRC fail frame", _recvBuf, HIMPEL_PACKET_SIZE);
        printf("[AP] FRC mismatch calc=0x%02X recv=0x%02X\r\n", c_frc, _recvBuf[22]);
#endif
    } else {
#if SHELTER_RS485_DEBUG_LOG
        printf("[AP] ACK timeout CMD=0x%02X DA=0x%02X RxState=0x%02lX isr_bytes=%lu\r\n",
               (unsigned)cmd, (unsigned)da, (unsigned long)huart3.RxState,
               (unsigned long)_ap_rx_isr_cnt);
#endif
    }

    if (ret != 0 && cmd == CMD_REQ_STATUS) {
        if (osMutexAcquire(mqtt_mutex_id, 100) == osOK) {
            dev_status.ap.is_connected = false;
            osMutexRelease(mqtt_mutex_id);
        }
    }

done:
    osMutexRelease(ap_bus_mutex);
    return ret;
}

/* ======================================================================
   [5. 수신 상태 머신 (UART ISR에서 호출)]
   ====================================================================== */
void HimpelSmInput(uint8_t input)
{
    if (_dataIndex >= RX_BUFFER_SIZE) HimpelSmReset();
    _dataBuf[_dataIndex++] = input;

    switch (_nextState) {
        case HIMPEL_SM_STX:
            if (input == HIMPEL_STX) _nextState = HIMPEL_SM_HD;
            else _dataIndex = 0;
            break;

        case HIMPEL_SM_HD:
            if (input == HIMPEL_HEADER) _nextState = HIMPEL_SM_DA;
            else HimpelSmReset();
            break;

        case HIMPEL_SM_DA:   _nextState = HIMPEL_SM_SA;   break;
        case HIMPEL_SM_SA:   _nextState = HIMPEL_SM_CMD;  break;
        case HIMPEL_SM_CMD:  _nextState = HIMPEL_SM_DATA; break;

        case HIMPEL_SM_DATA:
            if (_dataIndex >= 5 + HIMPEL_DATA_SIZE) _nextState = HIMPEL_SM_FRC;
            break;

        case HIMPEL_SM_FRC:  _nextState = HIMPEL_SM_EOP;  break;

        case HIMPEL_SM_EOP:
            if (input == HIMPEL_EOP) {
                memcpy(_recvBuf, _dataBuf, HIMPEL_PACKET_SIZE);
                _received = 1;
            } else {
#if SHELTER_RS485_DEBUG_LOG
                printf("[AP] EOP mismatch (got 0x%02X)\r\n", input);
#endif
            }
            HimpelSmReset();
            break;
    }
}

/* ======================================================================
   [6. 제어 API (Setter)] — 모두 Mutex를 내부에서 관리
   ★ 수정: PrepareAPControl의 osWaitForever → 200ms 타임아웃으로 변경.
   ====================================================================== */

/**
 * @brief 현재 dev_status.ap 를 기반으로 제어 메시지 초기화
 * @return 0: 성공, -1: Mutex 획득 실패
 */
static int PrepareAPControl(HimpelControlMessage* msg)
{
    memset(msg, 0, sizeof(HimpelControlMessage));

    /* ★ osWaitForever → 200ms 타임아웃 (데드락 방지) */
    if (osMutexAcquire(mqtt_mutex_id, 200) != osOK) return -1;

    msg->ubPower      = (strcmp(dev_status.ap.pwr,  "ON")     == 0);
    msg->ubAutomatic  = (strcmp(dev_status.ap.mode, "AUTO")   == 0);
    msg->ubAirClean   = (strcmp(dev_status.ap.mode, "CLEAN")  == 0);
    msg->ubVentilation= (strcmp(dev_status.ap.mode, "VENT")   == 0);
    msg->ubBypass     = (strcmp(dev_status.ap.mode, "BYPASS") == 0);
    msg->ubModeHeater = (strcmp(dev_status.ap.mode, "HEATER") == 0);
    msg->ubUV         = dev_status.ap.uv_on;

    int spd = dev_status.ap.fan_speed;
    if      (spd == 4) msg->ubFanTurbo = 1;
    else if (spd == 3) msg->ubFanHigh  = 1;
    else if (spd == 2) msg->ubFanMid   = 1;
    else if (spd == 1) msg->ubFanLow   = 1;
    else               msg->ubFanOff   = 1;

    osMutexRelease(mqtt_mutex_id);
    return 0;
}

int setAPPower(int on)
{
    HimpelControlMessage msg;
    if (PrepareAPControl(&msg) != 0) return -1;
    msg.ubPower  = on ? 1 : 0;
    msg.ubFanOff = on ? 0 : 1;
    return HimpelSendData(ID_SINGLE, CMD_CONTROL, &msg);
}

int setAPMode(const char* mode)
{
    HimpelControlMessage msg;
    if (PrepareAPControl(&msg) != 0) return -1;
    /* 모드 비트 초기화 후 해당 모드만 설정 */
    msg.ubAutomatic = msg.ubAirClean = msg.ubVentilation = msg.ubBypass = 0;
    msg.ubModeHeater = 0;
    if      (strcmp(mode, "AUTO")   == 0 || strcmp(mode, "MANUAL") == 0) msg.ubAutomatic   = 1;
    else if (strcmp(mode, "CLEAN")  == 0) msg.ubAirClean    = 1;
    else if (strcmp(mode, "VENT")   == 0) msg.ubVentilation = 1;
    else if (strcmp(mode, "BYPASS") == 0) msg.ubBypass      = 1;
    else if (strcmp(mode, "HEATER") == 0) msg.ubModeHeater  = 1;
    else return -1;
    return HimpelSendData(ID_SINGLE, CMD_CONTROL, &msg);
}

int setAPSpeed(int speed)
{
    if (speed < 0 || speed > 4) {
        return -1;
    }
    HimpelControlMessage msg;
    if (PrepareAPControl(&msg) != 0) return -1;
    /* 풍량 비트 초기화 후 해당 속도만 설정 */
    msg.ubFanOff = msg.ubFanLow = msg.ubFanMid = msg.ubFanHigh = msg.ubFanTurbo = 0;
    if      (speed == 4) msg.ubFanTurbo = 1;
    else if (speed == 3) msg.ubFanHigh  = 1;
    else if (speed == 2) msg.ubFanMid   = 1;
    else if (speed == 1) msg.ubFanLow   = 1;
    else               { msg.ubFanOff   = 1; msg.ubPower = 0; } // 정지
    return HimpelSendData(ID_SINGLE, CMD_CONTROL, &msg);
}

int setAPUV(int on)
{
    HimpelControlMessage msg;
    if (PrepareAPControl(&msg) != 0) return -1;
    msg.ubUV = on ? 1 : 0;
    return HimpelSendData(ID_SINGLE, CMD_CONTROL, &msg);
}

int setAPFilterReset(int on)
{
    if (on == 0) return 0;
    HimpelControlMessage msg;
    if (PrepareAPControl(&msg) != 0) return -1;
    msg.ubFilterAlarmOccur = 0;
    return HimpelSendData(ID_SINGLE, CMD_CONTROL, &msg);
}

int setAPBypass(int on)
{
    HimpelControlMessage msg;
    if (PrepareAPControl(&msg) != 0) return -1;
    msg.ubBypass = on ? 1 : 0;
    if (on) { msg.ubAutomatic = 0; msg.ubVentilation = 0; } // 모드 충돌 방지
    return HimpelSendData(ID_SINGLE, CMD_CONTROL, &msg);
}

int setAPTimer(int minutes)
{
    HimpelControlMessage msg;
    if (PrepareAPControl(&msg) != 0) return -1;
    msg.reserved[0] = (uint8_t)minutes;
    return HimpelSendData(ID_SINGLE, CMD_CONTROL, &msg);
}

int setAPAll(int pwr, const char* mode, int speed, int uv,
             int filter_reset, int bypass, int timer_minutes)
{
    if (speed < 0 || speed > 4) {
        return -1;
    }
    if (timer_minutes < 0 || timer_minutes > 255) {
        return -1;
    }
    HimpelControlMessage msg;
    if (PrepareAPControl(&msg) != 0) return -1;

    msg.ubPower = pwr ? 1 : 0;
    msg.ubAutomatic = msg.ubAirClean = msg.ubVentilation = msg.ubBypass = msg.ubModeHeater = 0;
    if      (strcmp(mode, "AUTO")   == 0 || strcmp(mode, "MANUAL") == 0) msg.ubAutomatic   = 1;
    else if (strcmp(mode, "CLEAN")  == 0) msg.ubAirClean    = 1;
    else if (strcmp(mode, "VENT")   == 0) msg.ubVentilation = 1;
    else if (strcmp(mode, "BYPASS") == 0) msg.ubBypass      = 1;
    else if (strcmp(mode, "HEATER") == 0) msg.ubModeHeater  = 1;
    else return -1;

    msg.ubFanOff = msg.ubFanLow = msg.ubFanMid = msg.ubFanHigh = msg.ubFanTurbo = 0;
    if      (speed == 4) msg.ubFanTurbo = 1;
    else if (speed == 3) msg.ubFanHigh  = 1;
    else if (speed == 2) msg.ubFanMid   = 1;
    else if (speed == 1) msg.ubFanLow   = 1;
    else               { msg.ubFanOff   = 1; if (!pwr) msg.ubPower = 0; }

    msg.ubUV = uv ? 1 : 0;
    if (filter_reset) msg.ubFilterAlarmOccur = 0;
    if (bypass) {
        msg.ubBypass = 1;
        msg.ubAutomatic = 0;
        msg.ubVentilation = 0;
    }
    msg.reserved[0] = (uint8_t)timer_minutes;

    return HimpelSendData(ID_SINGLE, CMD_CONTROL, &msg);
}

/* ======================================================================
   [7. 조회 API (Getter)]
   ====================================================================== */
int getFanStatus(void)
{
    return HimpelSendData(ID_SINGLE, CMD_REQ_STATUS, NULL);
}

int requestAPUpdate(void)
{
    return getFanStatus();
}

void getAPStatusAll(APData* dest)
{
    if (dest == NULL) return;
    if (osMutexAcquire(mqtt_mutex_id, 200) != osOK) return;
    *dest = dev_status.ap;
    osMutexRelease(mqtt_mutex_id);
}

int getAPPower(void)
{
    int pwr;
    if (osMutexAcquire(mqtt_mutex_id, 200) != osOK) return -1;
    pwr = (strcmp(dev_status.ap.pwr, "ON") == 0) ? 1 : 0;
    osMutexRelease(mqtt_mutex_id);
    return pwr;
}

int getAPSpeed(void)
{
    int speed;
    if (osMutexAcquire(mqtt_mutex_id, 200) != osOK) return -1;
    speed = dev_status.ap.fan_speed;
    osMutexRelease(mqtt_mutex_id);
    return speed;
}

int getAPCo2(void)
{
    int co2;
    if (osMutexAcquire(mqtt_mutex_id, 200) != osOK) return -1;
    co2 = dev_status.ap.co2;
    osMutexRelease(mqtt_mutex_id);
    return co2;
}

int getAPDust(void)
{
    int dust;
    if (osMutexAcquire(mqtt_mutex_id, 200) != osOK) return -1;
    dust = dev_status.ap.dust_pm25;
    osMutexRelease(mqtt_mutex_id);
    return dust;
}

/**
 * @brief 온도 조회
 * @param indoor  1: 실내 온도, 0: 실외 온도
 * @return 온도 (°C), Mutex 실패 시 -99.0f
 * ★ 추가: himpel.h에 선언되어 있었으나 구현이 없었습니다.
 */
float getAPTemperature(int indoor)
{
    float temp;
    if (osMutexAcquire(mqtt_mutex_id, 200) != osOK) return -99.0f;
    temp = indoor ? dev_status.ap.temp_in : dev_status.ap.temp_out;
    osMutexRelease(mqtt_mutex_id);
    return temp;
}

bool isAPUvActive(void)
{
    bool uv;
    if (osMutexAcquire(mqtt_mutex_id, 200) != osOK) return false;
    uv = dev_status.ap.uv_on;
    osMutexRelease(mqtt_mutex_id);
    return uv;
}

int getAPFilterLife(void)
{
    int life;
    if (osMutexAcquire(mqtt_mutex_id, 200) != osOK) return -1;
    life = dev_status.ap.filter_life;
    osMutexRelease(mqtt_mutex_id);
    return life;
}

void getAPModeString(char* dest)
{
    if (dest == NULL) return;
    if (osMutexAcquire(mqtt_mutex_id, 200) != osOK) return;
    strcpy(dest, dev_status.ap.mode);
    osMutexRelease(mqtt_mutex_id);
}
