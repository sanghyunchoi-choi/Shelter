/*
 * ds3231m.c
 *
 *  Created on: May 19, 2024
 *      Author: choi
 */

#include "ds3231m.h"
#include "socket.h"
#include "sntp.h"
//RTC

#define DS3231_ADDR  (0x68 << 1)
#define SNTP_SOCKET_NUM    2
extern RTC_HandleTypeDef hrtc;

// 각 월별 최대 일수 배열 (평년 기준, 윤년은 아래 코드에서 별도 처리)
static const uint8_t DaysInMonths[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};


I2C_HandleTypeDef *i2c;

static uint8_t B2D(uint8_t bcd);
static uint8_t D2B(uint8_t decimal);

void DS3231_Init(I2C_HandleTypeDef *handle)
{
	i2c = handle;
}

bool DS3231_GetTime(_RTC *rtc)
{
	uint8_t startAddr = DS3231_REG_TIME;
	uint8_t buffer[7] = {0,0,0,0,0,0,0};

	if(HAL_I2C_Master_Transmit(i2c, DS3231_ADDR, &startAddr, 1, HAL_MAX_DELAY) != HAL_OK) return false;
	//HAL_Delay(2);
	if(HAL_I2C_Master_Receive(i2c, DS3231_ADDR, buffer, sizeof(buffer), HAL_MAX_DELAY) != HAL_OK) return false;
	//HAL_Delay(2);

	rtc->Sec = B2D(buffer[0] & 0x7F);
	rtc->Min = B2D(buffer[1] & 0x7F);
	rtc->Hour = B2D(buffer[2] & 0x3F);
	rtc->DaysOfWeek = buffer[3] & 0x07;
	rtc->Date = B2D(buffer[4] & 0x3F);
	rtc->Month = B2D(buffer[5] & 0x1F);
	rtc->Year = B2D(buffer[6]);

	return true;
}

bool DS3231_SetTime(_RTC *rtc)
{
	uint8_t startAddr = DS3231_REG_TIME;
	uint8_t buffer[8] = {startAddr, D2B(rtc->Sec), D2B(rtc->Min), D2B(rtc->Hour), rtc->DaysOfWeek, D2B(rtc->Date), D2B(rtc->Month), D2B(rtc->Year)};
	if(HAL_I2C_Master_Transmit(i2c, DS3231_ADDR, buffer, sizeof(buffer), HAL_MAX_DELAY) != HAL_OK) return false;

	return true;
}

bool DS3231_ReadTemperature(float *temp)
{
	uint8_t startAddr = DS3231_REG_TEMP;
	uint8_t buffer[2] = {0,};

	if(HAL_I2C_Master_Transmit(i2c, DS3231_ADDR, &startAddr, 1, HAL_MAX_DELAY) != HAL_OK) return false;
	if(HAL_I2C_Master_Receive(i2c, DS3231_ADDR, buffer, sizeof(buffer), HAL_MAX_DELAY) != HAL_OK) return false;

	int16_t value = (buffer[0] << 8) | (buffer[1]);
	value = (value >> 6);

	*temp = value / 4.0f;
	return true;
}


bool DS3231_SetAlarm1(uint8_t mode, uint8_t date, uint8_t hour, uint8_t min, uint8_t sec)
{
	uint8_t alarmSecond = D2B(sec);
	uint8_t alarmMinute = D2B(min);
	uint8_t alarmHour = D2B(hour);
	uint8_t alarmDate = D2B(date);

	switch(mode)
	{
	case ALARM_MODE_ALL_MATCHED:
		break;
	case ALARM_MODE_HOUR_MIN_SEC_MATCHED:
		alarmDate |= 0x80;
		break;
	case ALARM_MODE_MIN_SEC_MATCHED:
		alarmDate |= 0x80;
		alarmHour |= 0x80;
		break;
	case ALARM_MODE_SEC_MATCHED:
		alarmDate |= 0x80;
		alarmHour |= 0x80;
		alarmMinute |= 0x80;
		break;
	case ALARM_MODE_ONCE_PER_SECOND:
		alarmDate |= 0x80;
		alarmHour |= 0x80;
		alarmMinute |= 0x80;
		alarmSecond |= 0x80;
		break;
	default:
		break;
	}

	/* Write Alarm Registers */
	uint8_t startAddr = DS3231_REG_ALARM1;
	uint8_t buffer[5] = {startAddr, alarmSecond, alarmMinute, alarmHour, alarmDate};
	if(HAL_I2C_Master_Transmit(i2c, DS3231_ADDR, buffer, sizeof(buffer), HAL_MAX_DELAY) != HAL_OK) return false;

	/* Enable Alarm1 at Control Register */
	uint8_t ctrlReg = 0x00;
	ReadRegister(DS3231_REG_CONTROL, &ctrlReg);
	ctrlReg |= DS3231_CON_A1IE;
	ctrlReg |= DS3231_CON_INTCN;
	WriteRegister(DS3231_REG_CONTROL, ctrlReg);

	return true;
}

bool DS3231_ClearAlarm1()
{
	uint8_t ctrlReg;
	uint8_t statusReg;

	/* Clear Control Register */
	ReadRegister(DS3231_REG_CONTROL, &ctrlReg);
	ctrlReg &= ~DS3231_CON_A1IE;
	WriteRegister(DS3231_REG_CONTROL, ctrlReg);

	/* Clear Status Register */
	ReadRegister(DS3231_REG_STATUS, &statusReg);
	statusReg &= ~DS3231_STA_A1F;
	WriteRegister(DS3231_REG_STATUS, statusReg);

	return true;
}

bool ReadRegister(uint8_t regAddr, uint8_t *value)
{
	if(HAL_I2C_Master_Transmit(i2c, DS3231_ADDR, &regAddr, 1, HAL_MAX_DELAY) != HAL_OK) return false;
	if(HAL_I2C_Master_Receive(i2c, DS3231_ADDR, value, 1, HAL_MAX_DELAY) != HAL_OK) return false;

	return true;
}

bool WriteRegister(uint8_t regAddr, uint8_t value)
{
	uint8_t buffer[2] = {regAddr, value};
	if(HAL_I2C_Master_Transmit(i2c, DS3231_ADDR, buffer, sizeof(buffer), HAL_MAX_DELAY) != HAL_OK) return false;

	return true;
}
static uint8_t B2D(uint8_t bcd)
{
	return (bcd >> 4) * 10 + (bcd & 0x0F);
}

static uint8_t D2B(uint8_t decimal)
{
	return (((decimal / 10) << 4) | (decimal % 10));
}


int W5500_Sync_RTC_From_NTP(void) {
    // 💡 구글 및 국내에서 가장 신뢰성 높은 고정 공용 NTP 서버 IP 리스트 (안전 최우선 이중화)
    uint8_t ntp_server_list[3][4] = {
        {216, 239, 35, 4},   // time.google.com (구글 애니캐스트 IP 4번)
        {216, 239, 35, 0},   // time.google.com (기존 0번 서브)
        {203, 247, 181, 1}   // time.bora.net (국내 표준시 백업 코어)
    };

    datetime ntp_time;
    uint8_t sntp_buf[64];
    int success = 0;

    // 1. 소켓 안정성을 확보하기 위해 진입 전 확실히 Close 및 소량 대기
    close(SNTP_SOCKET_NUM);
    uint32_t sntp_wt = HAL_GetTick();
    while(getSn_SR(SNTP_SOCKET_NUM) != SOCK_CLOSED && (HAL_GetTick() - sntp_wt < 100)) {
        HAL_Delay(1);
    }
    HAL_Delay(10);

    // 2. 준비된 NTP 서버들을 순회하며 동기화 시도
    for (int server_idx = 0; server_idx < 3; server_idx++) {
        // 타임존 연산 버그 방지를 위해 UTC(0) 주입
        SNTP_init(SNTP_SOCKET_NUM, ntp_server_list[server_idx], 0, sntp_buf);

        int retry = 0;
        while (retry < 3) { // 서버당 3번씩 빠르게 재시도
            if (SNTP_run(&ntp_time) == 1) {
                success = 1;
                break;
            }
            retry++;
            HAL_Delay(300); // 300ms 대기
        }

        // 실패 시 W5500 소켓 레지스터를 확실히 정리하고 다음 서버로 이동
        close(SNTP_SOCKET_NUM);
        uint32_t close_wt = HAL_GetTick();
        while(getSn_SR(SNTP_SOCKET_NUM) != SOCK_CLOSED && (HAL_GetTick() - close_wt < 100)) {
            HAL_Delay(1);
        }

        if (success) break; // 동기화 성공 시 루프 탈출
    }

    // 모든 서버가 응답하지 않은 경우 안전하게 종료
    if (!success) {
        printf("[DROP] All SNTP Servers Timeout. Network/Port 123 Blocked.\r\n");
        return 0;
    }

    // =========================================================================
    // 💡 [수정 완료] 수신된 UTC 표준시에 대한민국 시간 (+9시간) 정확히 가산
    // =========================================================================
    int year  = ntp_time.yy;
    int month = ntp_time.mo;
    int day   = ntp_time.dd;
    int hour  = ntp_time.hh + 21; // ★ 기존 +21에서 대한민국 표준시 +9로 정상 수정
    int min   = ntp_time.mm;
    int sec   = ntp_time.ss;

    // 1. 시간이 24시를 넘었을 때 (다음날로 전환 처리)
    if (hour >= 24) {
        hour -= 24;
        day += 1;

        // 💡 [안전 필터] 수신된 월 데이터 유효성 검증
        if (month < 1 || month > 12) {
            printf("[DROP] Invalid Month detected from NTP data: %d\r\n", month);
            return 0;
        }

        // 윤년 판단
        int isLeapYear = ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) ? 1 : 0;
        int maxDay = DaysInMonths[month];
        if (month == 2 && isLeapYear) {
            maxDay = 29;
        }

        // 2. 일이 해당 월의 최대 일수를 넘었을 때 (다음달로 전환 처리)
        if (day > maxDay) {
            day = 1;
            month += 1;

            // 3. 월이 12월을 넘었을 때 (다음해로 전환 처리)
            if (month > 12) {
                month = 1;
                year += 1;
            }
        }
    }

    // -------------------------------------------------------------------------
    // 외장 DS3231M 고정밀 RTC 칩 주입
    // -------------------------------------------------------------------------
    _RTC ds_rtc;
    ds_rtc.Year       = (uint8_t)(year - 2000);
    ds_rtc.Month      = (uint8_t)month;
    ds_rtc.Date       = (uint8_t)day;
    ds_rtc.Hour       = (uint8_t)hour;
    ds_rtc.Min        = (uint8_t)min;
    ds_rtc.Sec        = (uint8_t)sec;
    ds_rtc.DaysOfWeek = 1;

    if(DS3231_SetTime(&ds_rtc) != true) {
        printf("[WARN] External DS3231M Calibration Failed.\r\n");
    }

    // -------------------------------------------------------------------------
    // STM32H7 내부 MCU Hardware RTC 레지스터 주입
    // -------------------------------------------------------------------------
    RTC_TimeTypeDef sTime = {0};
    RTC_DateTypeDef sDate = {0};

    sTime.Hours   = (uint8_t)hour;
    sTime.Minutes = (uint8_t)min;
    sTime.Seconds = (uint8_t)sec;
    sTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    sTime.StoreOperation = RTC_STOREOPERATION_RESET;

    sDate.Year    = (uint8_t)(year - 2000);
    sDate.Month   = (uint8_t)month;
    sDate.Date    = (uint8_t)day;
    sDate.WeekDay = RTC_WEEKDAY_MONDAY;

    if (HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN) != HAL_OK ||
        HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN) != HAL_OK) {
        printf("[DROP] Failed to update STM32H7 Internal RTC.\r\n");
        return 0;
    }

    return 1;
}

/**
 * @brief 부팅 시 외장 칩(DS3231M)에 보존되어 있던 시간을 읽어와 MCU 내부 RTC에 복사하는 함수
 * @note main.c의 초기화 시퀀스 또는 app.c 태스크 시작 전에 단 한 번 구동합니다.
 */
/**
 * @brief 부팅 시 외장 고정밀 칩(DS3231M)에 저장되어 있던 시간을 읽어와 MCU 내부 Hardware RTC에 완벽 복사하는 함수
 */
void Boot_Sync_Internal_RTC_From_DS3231(void) {
    _RTC ds_rtc;

    //printf("[BOOT] Checking Saved Time from High-Precision External RTC...\r\n");

    // 1. I2C3 버스를 통해 외장 DS3231M 레지스터 값 추출
    if (DS3231_GetTime(&ds_rtc) == true) {
        printf("\r\n[BOOT] Ext-RTC Read Success: 20%02d-%02d-%02d %02d:%02d:%02d\r\n",
                ds_rtc.Year, ds_rtc.Month, ds_rtc.Date, ds_rtc.Hour, ds_rtc.Min, ds_rtc.Sec);

        // ⚠️ 예외 처리: 배터리 방전 등으로 외장 칩이 공장 초기화 상태(2000년 이전 등)라면 복사를 건너뜁니다.
        if (ds_rtc.Year == 0 || ds_rtc.Month == 0 || ds_rtc.Date == 0) {
            printf("[BOOT] [WARN] External RTC contains uninitialized time. Skip copying.\r\n");
            return;
        }

        RTC_TimeTypeDef sTime = {0};
        RTC_DateTypeDef sDate = {0};

        // STM32H7 내부 RTC 구조체 규격에 맞게 바인딩
        sTime.Hours          = ds_rtc.Hour;
        sTime.Minutes        = ds_rtc.Min;
        sTime.Seconds        = ds_rtc.Sec;
        sTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
        sTime.StoreOperation = RTC_STOREOPERATION_RESET;

        sDate.Year           = ds_rtc.Year;
        sDate.Month          = ds_rtc.Month;
        sDate.Date           = ds_rtc.Date;
        sDate.WeekDay        = RTC_WEEKDAY_MONDAY; // mbedTLS 주간 검증 우회용 고정값

        // ⚠️ STM32H7 RTC 핵심 규칙: 레지스터 하드웨어 동기화를 위해 반드시 SetTime을 먼저 호출한 후 SetDate를 호출해야 합니다.
        if (HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN) != HAL_OK) {
            printf("[BOOT] [ERROR] HAL_RTC_SetTime Failed.\r\n");
            return;
        }

        if (HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN) != HAL_OK) {
            printf("[BOOT] [ERROR] HAL_RTC_SetDate Failed.\r\n");
            return;
        }

        //printf("[BOOT] Copied to STM32H7 Internal RTC. Network Validation Ready.\r\n");
    } else {
        printf("[BOOT] [ERROR] Failed to communicate with DS3231M via I2C3.\r\n");
    }
}
/**
 * @brief 현재 STM32H7 내부 Hardware RTC의 시간을 문자열 로그용 버퍼에 채워주는 함수
 * @param buf 문자열이 저장될 출력 버퍼 (최소 24바이트 이상 필요)
 * @param max_len 버퍼의 최대 크기
 */
void Get_Current_Time_Log_Str(char *buf, size_t max_len) {
    RTC_TimeTypeDef sTime = {0};
    RTC_DateTypeDef sDate = {0};

    // ⚠️ STM32H7 RTC 필수 보안 규칙:
    // 하드웨어 레지스터 잠금 및 그림자 레지스터(Shadow Register) 동기화를 위해
    // 반드시 HAL_RTC_GetTime을 "먼저" 호출하고, 직후에 HAL_RTC_GetDate를 "무조건" 호출해야 데이터 꼬임이 없습니다.
    HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);

    // 로그용 포맷 조립 (예: 2026-05-18 15:30:22)
    snprintf(buf, max_len, "20%02d-%02d-%02d %02d:%02d:%02d",
             sDate.Year, sDate.Month, sDate.Date,
             sTime.Hours, sTime.Minutes, sTime.Seconds);
}

/**
 * @brief 접두사(Prefix) 태그와 함께 실시간 타임스탬프를 프린트해주는 범용 매크로 대용 함수
 * @param tag 로그 성격 태그 (예: "MQTT", "SENSOR", "ERR")
 * @param msg 출력할 본문 메시지
 */
void Log_With_Time(const char *tag, const char *msg) {
    char time_str[24];
    Get_Current_Time_Log_Str(time_str, sizeof(time_str));
    printf("[%s] [%s] %s\r\n", time_str, tag, msg);
}
