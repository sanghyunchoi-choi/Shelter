################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Src/phri/25LC256.c \
../Core/Src/phri/crc.c \
../Core/Src/phri/cwt_th03s.c \
../Core/Src/phri/ds3231m.c \
../Core/Src/phri/hcsd.c \
../Core/Src/phri/himpel.c \
../Core/Src/phri/modbus_lg.c \
../Core/Src/phri/net_config.c \
../Core/Src/phri/power_board.c \
../Core/Src/phri/prov_config.c \
../Core/Src/phri/updm010ub.c 

OBJS += \
./Core/Src/phri/25LC256.o \
./Core/Src/phri/crc.o \
./Core/Src/phri/cwt_th03s.o \
./Core/Src/phri/ds3231m.o \
./Core/Src/phri/hcsd.o \
./Core/Src/phri/himpel.o \
./Core/Src/phri/modbus_lg.o \
./Core/Src/phri/net_config.o \
./Core/Src/phri/power_board.o \
./Core/Src/phri/prov_config.o \
./Core/Src/phri/updm010ub.o 

C_DEPS += \
./Core/Src/phri/25LC256.d \
./Core/Src/phri/crc.d \
./Core/Src/phri/cwt_th03s.d \
./Core/Src/phri/ds3231m.d \
./Core/Src/phri/hcsd.d \
./Core/Src/phri/himpel.d \
./Core/Src/phri/modbus_lg.d \
./Core/Src/phri/net_config.d \
./Core/Src/phri/power_board.d \
./Core/Src/phri/prov_config.d \
./Core/Src/phri/updm010ub.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Src/phri/%.o Core/Src/phri/%.su Core/Src/phri/%.cyclo: ../Core/Src/phri/%.c Core/Src/phri/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I../Core/Inc -I../Core/Src/phri -I../w5500/ioLibrary_Driver-master/Internet/SNTP -I../w5500/ioLibrary_Driver-master/Ethernet -I../w5500/ioLibrary_Driver-master/Application/loopback -I../w5500/ioLibrary_Driver-master/Application -I../w5500/ioLibrary_Driver-master/Application/multicast -I../w5500/ioLibrary_Driver-master/Ethernet/W5500 -I../w5500/ioLibrary_Driver-master/Internet/DHCP -I../w5500/ioLibrary_Driver-master/Internet/DNS -I../w5500/ioLibrary_Driver-master/Internet/MQTT -I../w5500/ioLibrary_Driver-master/Internet/httpServer -I../Middlewares/Third_Party/FreeRTOS/Source/include -I../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 -I../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-Src-2f-phri

clean-Core-2f-Src-2f-phri:
	-$(RM) ./Core/Src/phri/25LC256.cyclo ./Core/Src/phri/25LC256.d ./Core/Src/phri/25LC256.o ./Core/Src/phri/25LC256.su ./Core/Src/phri/crc.cyclo ./Core/Src/phri/crc.d ./Core/Src/phri/crc.o ./Core/Src/phri/crc.su ./Core/Src/phri/cwt_th03s.cyclo ./Core/Src/phri/cwt_th03s.d ./Core/Src/phri/cwt_th03s.o ./Core/Src/phri/cwt_th03s.su ./Core/Src/phri/ds3231m.cyclo ./Core/Src/phri/ds3231m.d ./Core/Src/phri/ds3231m.o ./Core/Src/phri/ds3231m.su ./Core/Src/phri/hcsd.cyclo ./Core/Src/phri/hcsd.d ./Core/Src/phri/hcsd.o ./Core/Src/phri/hcsd.su ./Core/Src/phri/himpel.cyclo ./Core/Src/phri/himpel.d ./Core/Src/phri/himpel.o ./Core/Src/phri/himpel.su ./Core/Src/phri/modbus_lg.cyclo ./Core/Src/phri/modbus_lg.d ./Core/Src/phri/modbus_lg.o ./Core/Src/phri/modbus_lg.su ./Core/Src/phri/net_config.cyclo ./Core/Src/phri/net_config.d ./Core/Src/phri/net_config.o ./Core/Src/phri/net_config.su ./Core/Src/phri/power_board.cyclo ./Core/Src/phri/power_board.d ./Core/Src/phri/power_board.o ./Core/Src/phri/power_board.su ./Core/Src/phri/prov_config.cyclo ./Core/Src/phri/prov_config.d ./Core/Src/phri/prov_config.o ./Core/Src/phri/prov_config.su ./Core/Src/phri/updm010ub.cyclo ./Core/Src/phri/updm010ub.d ./Core/Src/phri/updm010ub.o ./Core/Src/phri/updm010ub.su

.PHONY: clean-Core-2f-Src-2f-phri

