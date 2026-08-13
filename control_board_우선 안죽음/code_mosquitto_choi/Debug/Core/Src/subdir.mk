################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Src/app.c \
../Core/Src/app_loop.c \
../Core/Src/freertos.c \
../Core/Src/main.c \
../Core/Src/mqtt_handler.c \
../Core/Src/stm32h7xx_hal_msp.c \
../Core/Src/stm32h7xx_hal_timebase_tim.c \
../Core/Src/stm32h7xx_it.c \
../Core/Src/syscalls.c \
../Core/Src/sysmem.c \
../Core/Src/system_stm32h7xx.c \
../Core/Src/uart_test.c \
../Core/Src/w5500_ctrl.c 

OBJS += \
./Core/Src/app.o \
./Core/Src/app_loop.o \
./Core/Src/freertos.o \
./Core/Src/main.o \
./Core/Src/mqtt_handler.o \
./Core/Src/stm32h7xx_hal_msp.o \
./Core/Src/stm32h7xx_hal_timebase_tim.o \
./Core/Src/stm32h7xx_it.o \
./Core/Src/syscalls.o \
./Core/Src/sysmem.o \
./Core/Src/system_stm32h7xx.o \
./Core/Src/uart_test.o \
./Core/Src/w5500_ctrl.o 

C_DEPS += \
./Core/Src/app.d \
./Core/Src/app_loop.d \
./Core/Src/freertos.d \
./Core/Src/main.d \
./Core/Src/mqtt_handler.d \
./Core/Src/stm32h7xx_hal_msp.d \
./Core/Src/stm32h7xx_hal_timebase_tim.d \
./Core/Src/stm32h7xx_it.d \
./Core/Src/syscalls.d \
./Core/Src/sysmem.d \
./Core/Src/system_stm32h7xx.d \
./Core/Src/uart_test.d \
./Core/Src/w5500_ctrl.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Src/%.o Core/Src/%.su Core/Src/%.cyclo: ../Core/Src/%.c Core/Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I../Core/Inc -I"D:/2026.05.08/2025_project/2)smarttech/Project/Shelter/2026.05.25_final/MOS_version/control_board/code_mosquitto_choi/Core/Src/phri" -I"D:/2026.05.08/2025_project/2)smarttech/Project/Shelter/2026.05.25_final/MOS_version/control_board/code_mosquitto_choi/w5500/ioLibrary_Driver-master/Internet/SNTP" -I"D:/2026.05.08/2025_project/2)smarttech/Project/Shelter/2026.05.25_final/MOS_version/control_board/code_mosquitto_choi/w5500/ioLibrary_Driver-master/Ethernet" -I"D:/2026.05.08/2025_project/2)smarttech/Project/Shelter/2026.05.25_final/MOS_version/control_board/code_mosquitto_choi/w5500/ioLibrary_Driver-master/Application/loopback" -I"D:/2026.05.08/2025_project/2)smarttech/Project/Shelter/2026.05.25_final/MOS_version/control_board/code_mosquitto_choi/w5500/ioLibrary_Driver-master/Application" -I"D:/2026.05.08/2025_project/2)smarttech/Project/Shelter/2026.05.25_final/MOS_version/control_board/code_mosquitto_choi/w5500/ioLibrary_Driver-master/Application/multicast" -I"D:/2026.05.08/2025_project/2)smarttech/Project/Shelter/2026.05.25_final/MOS_version/control_board/code_mosquitto_choi/w5500/ioLibrary_Driver-master/Ethernet/W5500" -I"D:/2026.05.08/2025_project/2)smarttech/Project/Shelter/2026.05.25_final/MOS_version/control_board/code_mosquitto_choi/w5500/ioLibrary_Driver-master/Internet/DHCP" -I"D:/2026.05.08/2025_project/2)smarttech/Project/Shelter/2026.05.25_final/MOS_version/control_board/code_mosquitto_choi/w5500/ioLibrary_Driver-master/Internet/DNS" -I"D:/2026.05.08/2025_project/2)smarttech/Project/Shelter/2026.05.25_final/MOS_version/control_board/code_mosquitto_choi/w5500/ioLibrary_Driver-master/Internet/MQTT" -I"D:/2026.05.08/2025_project/2)smarttech/Project/Shelter/2026.05.25_final/MOS_version/control_board/code_mosquitto_choi/Middlewares/Third_Party/FreeRTOS/Source/include" -I"D:/2026.05.08/2025_project/2)smarttech/Project/Shelter/2026.05.25_final/MOS_version/control_board/code_mosquitto_choi/Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2" -I"D:/2026.05.08/2025_project/2)smarttech/Project/Shelter/2026.05.25_final/MOS_version/control_board/code_mosquitto_choi/Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F" -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-Src

clean-Core-2f-Src:
	-$(RM) ./Core/Src/app.cyclo ./Core/Src/app.d ./Core/Src/app.o ./Core/Src/app.su ./Core/Src/app_loop.cyclo ./Core/Src/app_loop.d ./Core/Src/app_loop.o ./Core/Src/app_loop.su ./Core/Src/freertos.cyclo ./Core/Src/freertos.d ./Core/Src/freertos.o ./Core/Src/freertos.su ./Core/Src/main.cyclo ./Core/Src/main.d ./Core/Src/main.o ./Core/Src/main.su ./Core/Src/mqtt_handler.cyclo ./Core/Src/mqtt_handler.d ./Core/Src/mqtt_handler.o ./Core/Src/mqtt_handler.su ./Core/Src/stm32h7xx_hal_msp.cyclo ./Core/Src/stm32h7xx_hal_msp.d ./Core/Src/stm32h7xx_hal_msp.o ./Core/Src/stm32h7xx_hal_msp.su ./Core/Src/stm32h7xx_hal_timebase_tim.cyclo ./Core/Src/stm32h7xx_hal_timebase_tim.d ./Core/Src/stm32h7xx_hal_timebase_tim.o ./Core/Src/stm32h7xx_hal_timebase_tim.su ./Core/Src/stm32h7xx_it.cyclo ./Core/Src/stm32h7xx_it.d ./Core/Src/stm32h7xx_it.o ./Core/Src/stm32h7xx_it.su ./Core/Src/syscalls.cyclo ./Core/Src/syscalls.d ./Core/Src/syscalls.o ./Core/Src/syscalls.su ./Core/Src/sysmem.cyclo ./Core/Src/sysmem.d ./Core/Src/sysmem.o ./Core/Src/sysmem.su ./Core/Src/system_stm32h7xx.cyclo ./Core/Src/system_stm32h7xx.d ./Core/Src/system_stm32h7xx.o ./Core/Src/system_stm32h7xx.su ./Core/Src/uart_test.cyclo ./Core/Src/uart_test.d ./Core/Src/uart_test.o ./Core/Src/uart_test.su ./Core/Src/w5500_ctrl.cyclo ./Core/Src/w5500_ctrl.d ./Core/Src/w5500_ctrl.o ./Core/Src/w5500_ctrl.su

.PHONY: clean-Core-2f-Src

