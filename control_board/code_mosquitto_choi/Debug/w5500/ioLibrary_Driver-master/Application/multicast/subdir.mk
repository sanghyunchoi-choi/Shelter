################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../w5500/ioLibrary_Driver-master/Application/multicast/multicast.c 

OBJS += \
./w5500/ioLibrary_Driver-master/Application/multicast/multicast.o 

C_DEPS += \
./w5500/ioLibrary_Driver-master/Application/multicast/multicast.d 


# Each subdirectory must supply rules for building sources it contributes
w5500/ioLibrary_Driver-master/Application/multicast/%.o w5500/ioLibrary_Driver-master/Application/multicast/%.su w5500/ioLibrary_Driver-master/Application/multicast/%.cyclo: ../w5500/ioLibrary_Driver-master/Application/multicast/%.c w5500/ioLibrary_Driver-master/Application/multicast/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I../Core/Inc -I../Core/Src/phri -I../w5500/ioLibrary_Driver-master/Internet/SNTP -I../w5500/ioLibrary_Driver-master/Ethernet -I../w5500/ioLibrary_Driver-master/Application/loopback -I../w5500/ioLibrary_Driver-master/Application -I../w5500/ioLibrary_Driver-master/Application/multicast -I../w5500/ioLibrary_Driver-master/Ethernet/W5500 -I../w5500/ioLibrary_Driver-master/Internet/DHCP -I../w5500/ioLibrary_Driver-master/Internet/DNS -I../w5500/ioLibrary_Driver-master/Internet/MQTT -I../w5500/ioLibrary_Driver-master/Internet/httpServer -I../Middlewares/Third_Party/FreeRTOS/Source/include -I../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 -I../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-w5500-2f-ioLibrary_Driver-2d-master-2f-Application-2f-multicast

clean-w5500-2f-ioLibrary_Driver-2d-master-2f-Application-2f-multicast:
	-$(RM) ./w5500/ioLibrary_Driver-master/Application/multicast/multicast.cyclo ./w5500/ioLibrary_Driver-master/Application/multicast/multicast.d ./w5500/ioLibrary_Driver-master/Application/multicast/multicast.o ./w5500/ioLibrary_Driver-master/Application/multicast/multicast.su

.PHONY: clean-w5500-2f-ioLibrary_Driver-2d-master-2f-Application-2f-multicast

