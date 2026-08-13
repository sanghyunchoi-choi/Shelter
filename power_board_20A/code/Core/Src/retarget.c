#include <stdio.h>
#include "main.h"

extern UART_HandleTypeDef huart2; // main.c에서 정의된 huart1 참조

/* printf 호출 시 내부적으로 이 함수를 통해 문자가 출력됩니다. */
int __io_putchar(int ch) {
    // UART1을 통해 1바이트 전송 (Timeout 10ms)
    HAL_UART_Transmit(&huart2, (uint8_t *)&ch, 1, 10);
    return ch;
}

/* scanf 등을 사용할 경우를 대비한 입력 리다이렉션 (선택사항) */
int __io_getchar(void) {
    uint8_t ch = 0;
    HAL_UART_Receive(&huart2, &ch, 1, HAL_MAX_DELAY);
    return ch;
}
