#ifndef INC_CWT_TH03S_H_
#define INC_CWT_TH03S_H_

#include "main.h"
#include "app.h" // THData 구조체 사용을 위해 포함

// Modbus RTU 설정 (CWT-TH03S 기본값)
#define MODBUS_SLAVE_ID      0x01
#define MODBUS_FUNC_READ     0x03
#define MODBUS_START_ADDR    0x0000
#define MODBUS_REG_COUNT     0x0002

// 외부 참조 변수 (UART 핸들)
extern UART_HandleTypeDef huart1;

// 함수 선언 - 매개변수 타입을 THData로 변경
void CWTTH03S_Modbus_Init(void);
HAL_StatusTypeDef CWTTH03S_Modbus_ReadSensor(THData *data);

#endif
