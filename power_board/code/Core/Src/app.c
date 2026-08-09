#include "app.h"
#include "hw_io.h"
#include "hw_adc.h"
#include "protocol.h"
#include <stdio.h>
#include <string.h>

/* 외부 정의 핸들러 (main.c에서 생성됨) */
extern UART_HandleTypeDef huart1; // 485 (9600)
extern ADC_HandleTypeDef hadc1;

/* 전역 변수 관리 */
uint8_t  g_board_id = 0;
uint8_t  g_relay_state[8] = {0,};  // 1: ON, 0: OFF
int16_t  g_current_val[8] = {0,};
uint8_t  g_tx_packet[64];

/* 수신 관련 변수 */
uint8_t  g_rx_data;
uint8_t  g_rx_buffer[64];
uint16_t g_rx_index = 0;
uint8_t  g_packet_received = 0;

/**
 * @brief RS485 데이터 송신 함수 (회로 논리 반영: LOW=TX, HIGH=RX)
 */
void RS485_SendData(const uint8_t *data, uint16_t length) {
	HW_RS485_TX_Mode(1); // PC13 LOW -> TX Enable
	HAL_Delay(2);        // 안정화 대기

	HAL_UART_Transmit(&huart1, (uint8_t*)data, length, 100);

	// UART 하드웨어 전송 완료(TC) 대기
	while (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_TC) == RESET);

	HW_RS485_TX_Mode(0); // PC13 HIGH -> RX Enable
}

/**
 * @brief 애플리케이션 초기화
 */
void App_Init(void) {
	// 1. 하드웨어 초기화 (ADC DMA 및 ID 읽기)
	HW_ADC_Init(&hadc1);
	g_board_id = HW_Get_BoardID();

	// 2. 초기 릴레이 상태 OFF 보장
	for(int i=1; i<=8; i++) HW_Relay_Set(i, 0);

	// 3. 디버그 출력 (UART2)
	printf("\r\n==================================\r\n");
	printf(" Power Board Monitoring System\r\n");
	printf(" Board ID : %d (0x%02X)\r\n", g_board_id, g_board_id);
	printf("==================================\r\n");

	// 4. 0점(Zero Point) 보정 수행
	// 릴레이가 모두 꺼진 상태의 순수 전압 베이스라인을 측정합니다.
	HW_ADC_Calibrate_Zero();

	// 5. 485 수신 인터럽트 활성화
	HW_RS485_TX_Mode(0);
	HAL_UART_Receive_IT(&huart1, &g_rx_data, 1);
}

/**
 * @brief 메인 루프 (1초 주기 보고 및 8채널 전체 모니터링)
 */
void App_Run(void) {
	static uint32_t last_report_tick = 0;

	if (HAL_GetTick() - last_report_tick >= 1000) {
		last_report_tick = HAL_GetTick();

		// [안전장치] 인터럽트가 죽었을 경우를 대비해 1초마다 다시 활성화
		if (huart1.RxState == HAL_UART_STATE_READY) {
			HAL_UART_Receive_IT(&huart1, &g_rx_data, 1);
		}

		printf("[STATUS] ID:%d | ", g_board_id);

		for (int i = 0; i < 8; i++) {
			// 릴레이 상태(g_relay_state)를 전달하여 자기장 간섭을 보정
			g_current_val[i] = HW_Get_Current_mA(i, g_relay_state[i]);

			// 디버그 출력
			printf("Ch%d:%.2fA ", i + 1, (float)g_current_val[i] / 1000.0f);
		}
		printf("\r\n");

		// 3. 30바이트 패킷 빌드 및 RS485 송신
		Build_Report_Packet(g_tx_packet, g_board_id, g_relay_state, g_current_val);
		RS485_SendData(g_tx_packet, 30);
	}

	if (g_packet_received) {
		printf("[DEBUG] Packet Received! CMD:0x%02X\r\n", g_rx_buffer[3]);
		App_Process_Packet();

		// 처리 완료 후 버퍼 및 인덱스 초기화
		g_packet_received = 0;
		g_rx_index = 0;

		// 다시 수신 시작
		HAL_UART_Receive_IT(&huart1, &g_rx_data, 1);
	}
}

/**
 * @brief 수신 패킷 해석 및 릴레이 제어
 */
void App_Process_Packet(void) {
	// 1. 기본 검증 (STX=0x02, ID 매칭)
	if (g_rx_buffer[0] != 0x02 || g_rx_buffer[1] != g_board_id) return;

	uint8_t len = g_rx_buffer[2];
	uint8_t cmd = g_rx_buffer[3];

	// 2. BCC 검증 (0번부터 ETX까지 XOR)
	uint8_t received_bcc = g_rx_buffer[len - 1];
	uint8_t calc_bcc = Calculate_BCC(g_rx_buffer, len - 1);

	if (calc_bcc != received_bcc) {
		printf("[RX] BCC Error (Calc:0x%02X, Recv:0x%02X)\r\n", calc_bcc, received_bcc);
		return;
	}

	// 3. 명령어 처리
	if (cmd == 0xAA) { // 전체 제어 (14바이트)
		printf("[RX] All Relay Control (Sequential Process)\r\n");

		// 1번 채널부터 8번 채널까지 정방향 순차 기동 루프 전개
		for (int i = 0; i < 8; i++) {
			// [4]~[11] 인덱스의 뒷자리 1비트를 목표 상태(Target State)로 추출
			uint8_t target_state = g_rx_buffer[4 + i] & 0x01;

			// [핵심 가드] 기존 보관 중인 상태(g_relay_state)와 새로운 목표 상태가 다를 때만 진입
			if (g_relay_state[i] != target_state) {

				// 1. 내부 전역 상태 데이터 배열 업데이트
				g_relay_state[i] = target_state;

				// 2. 물리 핀 제어 함수 호출 (1번부터 순사 진행)
				HW_Relay_Set(i + 1, target_state);
				printf(" >> Ch%d -> Changed to %s\r\n", i + 1, target_state ? "ON" : "OFF");

				// 3. 돌입 전류 및 전압 드롭(Drop)을 막기 위해 상태 변경 발생 시에만 200ms 대기
				HAL_Delay(2000);

			} else {
				// 이미 요청한 상태와 기존 하드웨어 상태가 똑같다면 딜레이 없이 즉시 다음 채널 패스 (Skip!)
				// 무부하 상태에서 패킷이 연속 유입되더라도 블록(Delay) 타이밍이 발생하지 않습니다.
			}
		}
		printf("[RX] All Relay Control Process Complete.\r\n");
	}
	else if (cmd == 0xCC) { // 개별 제어 (7바이트)
		uint8_t relay_id = g_rx_buffer[4];
		printf("[RX] Single Relay ID %d Request\r\n", relay_id);

		// (참고: 개별 제어 통로는 딜레이 없이 즉각 반응 상태 유지)
	}

}

/**
 * @brief UART 수신 인터럽트 콜백 (동기화 강화)
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
	if (huart->Instance == USART1) {
		// 동기화: 첫 바이트는 무조건 STX(02)여야 함
		if (g_rx_index == 0 && g_rx_data != 0x02) {
			HAL_UART_Receive_IT(&huart1, &g_rx_data, 1);
			return;
		}

		g_rx_buffer[g_rx_index++] = g_rx_data;

		// 패킷 헤더의 [2]번지(전체 데이터 길이)를 기준으로 패킷 완성 판단
		if (g_rx_index >= 3) {
			uint8_t target_len = g_rx_buffer[2];

			// 유효하지 않은 길이 예외 처리
			if (target_len < 5 || target_len > 60) {
				g_rx_index = 0;
			}
			else if (g_rx_index == target_len) {
				g_packet_received = 1;
			}
		}

		// 버퍼 가득 참 방지
		if (g_rx_index >= sizeof(g_rx_buffer)) g_rx_index = 0;

		// 패킷이 완성되지 않았다면 계속 수신
		if (!g_packet_received) {
			HAL_UART_Receive_IT(&huart1, &g_rx_data, 1);
		}
	}
}
