#include "hw_io.h"
#include <stdio.h>
#include <stdbool.h>
// 릴레이 핀 매핑 (1번~8번 순서)
GPIO_TypeDef* RLY_PORT[] = { RLY_1_GPIO_Port, RLY_2_GPIO_Port, RLY_3_GPIO_Port, RLY_4_GPIO_Port,
                             RLY_5_GPIO_Port, RLY_6_GPIO_Port, RLY_7_GPIO_Port, RLY_8_GPIO_Port };
uint16_t RLY_PIN[] = { RLY_1_Pin, RLY_2_Pin, RLY_3_Pin, RLY_4_Pin,
                       RLY_5_Pin, RLY_6_Pin, RLY_7_Pin, RLY_8_Pin };

void HW_Relay_Set(uint8_t ch, uint8_t state) {
    if (ch < 1 || ch > 8) return;
    HAL_GPIO_WritePin(RLY_PORT[ch-1], RLY_PIN[ch-1], state ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void App_Relay_Test(void) {
    //printf("\r\n[TEST] Starting Relay Sequence Test...\r\n");

    // 1. 순차적으로 ON (1번 -> 8번)
    printf(" >> Sequential ON: ");
    for (int i = 1; i <= 8; i++) {
        HW_Relay_Set(i, 1);
        printf("%d ", i);
        HAL_Delay(1000);
    }
    printf("DONE\r\n");

    HAL_Delay(500); // 모두 켜진 상태로 잠시 대기

    // 2. 순차적으로 OFF (1번 -> 8번)
    printf(" >> Sequential OFF: ");
    for (int i = 1; i <= 8; i++) {
        HW_Relay_Set(i, 0);
        printf("%d ", i);
        HAL_Delay(1000);
    }
    printf("DONE\r\n");

    printf("[TEST] Relay Test Completed.\r\n\r\n");
}

uint8_t HW_Get_BoardID(void) {
    uint8_t id = 0;

    // 외부 풀업이 있으므로 스위치를 ON 하면 핀 전압은 LOW(RESET)가 됩니다.
    // 스위치 ON(LOW) -> 비트 1 세팅
    if (HAL_GPIO_ReadPin(ID1_GPIO_Port, ID1_Pin) == GPIO_PIN_RESET) id |= 0x01;
    if (HAL_GPIO_ReadPin(ID2_GPIO_Port, ID2_Pin) == GPIO_PIN_RESET) id |= 0x02;
    if (HAL_GPIO_ReadPin(ID3_GPIO_Port, ID3_Pin) == GPIO_PIN_RESET) id |= 0x04;
    if (HAL_GPIO_ReadPin(ID4_GPIO_Port, ID4_Pin) == GPIO_PIN_RESET) id |= 0x08;

    return id;
}

void HW_RS485_TX_Mode(uint8_t is_tx) {
    if (is_tx) {
        // 송신 모드: PC13을 LOW로 (Q4 Off -> DE/RE High)
        HAL_GPIO_WritePin(ctrl_485_GPIO_Port, ctrl_485_Pin, GPIO_PIN_RESET);
    } else {
        // 수신 모드: PC13을 HIGH로 (Q4 On -> DE/RE Low)
        HAL_GPIO_WritePin(ctrl_485_GPIO_Port, ctrl_485_Pin, GPIO_PIN_SET);
    }
}
