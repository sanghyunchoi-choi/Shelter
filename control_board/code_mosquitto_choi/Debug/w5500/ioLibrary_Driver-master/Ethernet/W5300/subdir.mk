################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../w5500/ioLibrary_Driver-master/Ethernet/W5300/w5300.c 

OBJS += \
./w5500/ioLibrary_Driver-master/Ethernet/W5300/w5300.o 

C_DEPS += \
./w5500/ioLibrary_Driver-master/Ethernet/W5300/w5300.d 


# Each subdirectory must supply rules for building sources it contributes
w5500/ioLibrary_Driver-master/Ethernet/W5300/%.o w5500/ioLibrary_Driver-master/Ethernet/W5300/%.su w5500/ioLibrary_Driver-master/Ethernet/W5300/%.cyclo: ../w5500/ioLibrary_Driver-master/Ethernet/W5300/%.c w5500/ioLibrary_Driver-master/Ethernet/W5300/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I../Core/Inc -I"D:/2025_project/2)smarttech/Project/Shelter/Control Board/code_choi/w5500/ioLibrary_Driver-master/Ethernet" -I"D:/2025_project/2)smarttech/Project/Shelter/Control Board/code_choi/w5500/ioLibrary_Driver-master/Application/loopback" -I"D:/2025_project/2)smarttech/Project/Shelter/Control Board/code_choi/w5500/ioLibrary_Driver-master/Application" -I"D:/2025_project/2)smarttech/Project/Shelter/Control Board/code_choi/w5500/ioLibrary_Driver-master/Application/multicast" -I"D:/2025_project/2)smarttech/Project/Shelter/Control Board/code_choi/w5500/ioLibrary_Driver-master/Ethernet/W5500" -I"D:/2025_project/2)smarttech/Project/Shelter/Control Board/code_choi/w5500/ioLibrary_Driver-master/Internet/DHCP" -I"D:/2025_project/2)smarttech/Project/Shelter/Control Board/code_choi/w5500/ioLibrary_Driver-master/Internet/DNS" -I"D:/2025_project/2)smarttech/Project/Shelter/Control Board/code_choi/w5500/ioLibrary_Driver-master/Internet/MQTT" -I"D:/2025_project/2)smarttech/Project/Shelter/Control Board/code_choi/Middlewares/Third_Party/FreeRTOS/Source/include" -I"D:/2025_project/2)smarttech/Project/Shelter/Control Board/code_choi/Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2" -I"D:/2025_project/2)smarttech/Project/Shelter/Control Board/code_choi/Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F" -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-w5500-2f-ioLibrary_Driver-2d-master-2f-Ethernet-2f-W5300

clean-w5500-2f-ioLibrary_Driver-2d-master-2f-Ethernet-2f-W5300:
	-$(RM) ./w5500/ioLibrary_Driver-master/Ethernet/W5300/w5300.cyclo ./w5500/ioLibrary_Driver-master/Ethernet/W5300/w5300.d ./w5500/ioLibrary_Driver-master/Ethernet/W5300/w5300.o ./w5500/ioLibrary_Driver-master/Ethernet/W5300/w5300.su

.PHONY: clean-w5500-2f-ioLibrary_Driver-2d-master-2f-Ethernet-2f-W5300

