//*****************************************************************************
//! \file mqtt_interface.c
//! \brief Paho MQTT to WIZnet Chip interface implement file.
//! \details The process of porting an interface to use paho MQTT.
//! \version 1.0.0
//! \date 2016/12/06
//! \par  Revision history
//!       <2016/12/06> 1st Release
//!
//! \author Peter Bang & Justin Kim
//! \copyright
//!
//! Copyright (c)  2016, WIZnet Co., LTD.
//! All rights reserved.
//!
//! Redistribution and use in source and binary forms, with or without
//! modification, are permitted provided that the following conditions
//! are met:
//!
//!     * Redistributions of source code must retain the above copyright
//! notice, this list of conditions and the following disclaimer.
//!     * Redistributions in binary form must reproduce the above copyright
//! notice, this list of conditions and the following disclaimer in the
//! documentation and/or other materials provided with the distribution.
//!     * Neither the name of the <ORGANIZATION> nor the names of its
//! contributors may be used to endorse or promote products derived
//! from this software without specific prior written permission.
//!
//! THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
//! AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
//! IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
//! ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
//! LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
//! CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
//! SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
//! INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
//! CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
//! ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
//! THE POSSIBILITY OF SUCH DAMAGE.
//
//*****************************************************************************

/* ★ 2026-08-09 빌드 오류 수정: main.h(STM32 HAL)를 wizchip_conf.h/socket.h보다
 * 먼저 include 해야 합니다. w5500.h는 "IR" 등의 레지스터 이름을 전처리기
 * 매크로로 정의하는데, STM32H7 HAL 헤더(stm32h743xx.h)에는 FDCAN/CCU
 * 레지스터의 구조체 멤버 이름으로 똑같이 "IR"을 사용합니다. main.h를 나중에
 * include하면 이미 정의된 매크로가 구조체 멤버 선언까지 치환해버려
 * "expected identifier or '(' before numeric constant" 컴파일 에러가 납니다. */
#include "main.h"
#include "cmsis_os2.h"
#include "mqtt_interface.h"
#include "wizchip_conf.h"
#include "socket.h"

unsigned long MilliTimer;

/*
    @brief MQTT MilliTimer handler
    @note MUST BE register to your system 1m Tick timer handler.
*/
void MilliTimer_Handler(void) {
    MilliTimer++;
}

/*
    @brief Timer Initialize
    @param  timer : pointer to a Timer structure
           that contains the configuration information for the Timer.
*/
void TimerInit(Timer* timer) {
    timer->end_time = 0;
}

/*
    @brief expired Timer
    @param  timer : pointer to a Timer structure
           that contains the configuration information for the Timer.
*/
char TimerIsExpired(Timer* timer) {
    long left = timer->end_time - MilliTimer;
    return (left < 0);
}

/*
    @brief Countdown millisecond Timer
    @param  timer : pointer to a Timer structure
           that contains the configuration information for the Timer.
           timeout : setting timeout millisecond.
*/
void TimerCountdownMS(Timer* timer, unsigned int timeout) {
    timer->end_time = MilliTimer + timeout;
}

/*
    @brief Countdown second Timer
    @param  timer : pointer to a Timer structure
           that contains the configuration information for the Timer.
           timeout : setting timeout millisecond.
*/
void TimerCountdown(Timer* timer, unsigned int timeout) {
    timer->end_time = MilliTimer + (timeout * 1000);
}

/*
    @brief left millisecond Timer
    @param  timer : pointer to a Timer structure
           that contains the configuration information for the Timer.
*/
int TimerLeftMS(Timer* timer) {
    long left = timer->end_time - MilliTimer;
    return (left < 0) ? 0 : left;
}

/*
    @brief New network setting
    @param  n : pointer to a Network structure
           that contains the configuration information for the Network.
           sn : socket number where x can be (0..7).
    @retval None
*/
void NewNetwork(Network* n, int sn) {
    n->my_socket = sn;
    n->mqttread = w5x00_read;
    n->mqttwrite = w5x00_write;
    n->disconnect = w5x00_disconnect;
}

/*
    @brief read function
    @param  n : pointer to a Network structure
           that contains the configuration information for the Network.
           buffer : pointer to a read buffer.
           len : buffer length.
    @retval received data length or SOCKERR code
*/
int w5x00_read(Network* n, unsigned char* buffer, int len, long time) {

    if ((getSn_SR(n->my_socket) == SOCK_ESTABLISHED) && (getSn_RX_RSR(n->my_socket) > 0)) {
        return recv(n->my_socket, buffer, len);
    }

    return SOCK_ERROR;
}

/*
    @brief write function
    @param  n : pointer to a Network structure
           that contains the configuration information for the Network.
           buffer : pointer to a read buffer.
           len : buffer length.
    @retval length of data sent or SOCKERR code
*/
int w5x00_write(Network* n, unsigned char* buffer, int len, long time) {
    if (getSn_SR(n->my_socket) == SOCK_ESTABLISHED) {
        return send(n->my_socket, buffer, len);
    }

    return SOCK_ERROR;
}

/*
    @brief disconnect function
    @param  n : pointer to a Network structure
           that contains the configuration information for the Network.
*/
void w5x00_disconnect(Network* n) {
    disconnect(n->my_socket);
}

/*
    @brief connect network function
    @param  n : pointer to a Network structure
           that contains the configuration information for the Network.
           ip : server iP.
           port : server port.
    @retval SOCKOK code or SOCKERR code
*/
#if 0
// mqtt_interface.c 파일의 해당 함수 수정
int ConnectNetwork(Network* n, uint8_t* ip, uint16_t port) {
    // 로컬 포트를 0으로 설정하면 W5500이 비어있는 포트를 자동으로 할당합니다.
    uint16_t myport = 0;

    if (socket(n->my_socket, Sn_MR_TCP, myport, 0) != n->my_socket) {
        return SOCK_ERROR;
    }

    // 브로커(Server) IP와 포트(1883)로 연결 시도
    if (connect(n->my_socket, ip, port) != SOCK_OK) {
        return SOCK_ERROR;
    }

    return SOCK_OK;
}

#else
/* ============================================================================
 * ★ 2026-08-09 재접속 안정화 패치 (보조 안전장치)
 *
 * 근본 원인은 DHCP/MQTT 소켓 번호 충돌(config.h, w5500_ctrl.c 참고)이었지만,
 * 그와 별개로 ioLibrary의 connect()는 아래처럼 애플리케이션이 제어할 수 없는
 * busy-wait이며 타임아웃도 osDelay()도 없습니다 (Ethernet/socket.c 참고):
 *     while (getSn_SR(sn) != SOCK_ESTABLISHED) {
 *         if (getSn_IR(sn) & Sn_IR_TIMEOUT) { ...; return SOCKERR_TIMEOUT; }
 *         if (getSn_SR(sn) == SOCK_CLOSED)   { return SOCKERR_SOCKCLOSED; }
 *     }
 * 브로커가 응답하지 않는 등의 상황에서 이 내부 타임아웃을 100% 신뢰할 수
 * 없으므로, 이중 안전장치로 애플리케이션 레벨에 확정적 타임아웃(4초)을
 * 추가합니다. 타임아웃 시 소켓을 강제로 닫고 즉시 SOCK_ERROR를 반환해
 * 상위 재시도 루프(app.c)로 제어권을 돌려주므로 더 이상 무한정 멈춰있을
 * 수 없습니다. 매 폴링마다 osDelay(10)로 양보하여 다른 태스크(AC/AP RS485
 * 폴링 등)가 굶는 것도 방지합니다.
 * ============================================================================ */
#define SHELTER_CONNECT_TIMEOUT_MS  4000U

int ConnectNetwork(Network* n, uint8_t* ip, uint16_t port) {
    uint16_t myport = 12345;
    uint8_t  sn = n->my_socket;

    if (socket(sn, Sn_MR_TCP, myport, 0) != sn) {
        return SOCK_ERROR;
    }

    setSn_DIPR(sn, ip);
    setSn_DPORTR(sn, port);
    setSn_CR(sn, Sn_CR_CONNECT);
    while (getSn_CR(sn)) { /* 커맨드 레지스터 반영 대기 — 수십 us, 안전 */ }

    uint32_t start_tick = HAL_GetTick();
    for (;;) {
        uint8_t sr = getSn_SR(sn);

        if (sr == SOCK_ESTABLISHED) {
            return SOCK_OK;
        }
        if (sr == SOCK_CLOSED) {
            return SOCK_ERROR;
        }
        if (getSn_IR(sn) & Sn_IR_TIMEOUT) {
            setSn_IR(sn, Sn_IR_TIMEOUT);
            return SOCK_ERROR;
        }
        if ((HAL_GetTick() - start_tick) >= SHELTER_CONNECT_TIMEOUT_MS) {
            setSn_CR(sn, Sn_CR_CLOSE);
            while (getSn_CR(sn)) { }
            printf("[NET] connect() forced-timeout after %lums (last SR=0x%02X)\r\n",
                    (unsigned long)SHELTER_CONNECT_TIMEOUT_MS, sr);
            return SOCK_ERROR;
        }

        osDelay(10); /* busy-wait 금지: 반드시 양보하여 다른 태스크 기아 방지 */
    }
}
#endif
