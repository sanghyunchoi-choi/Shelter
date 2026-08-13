################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTConnectClient.c \
../w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTConnectServer.c \
../w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTDeserializePublish.c \
../w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTFormat.c \
../w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTPacket.c \
../w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTSerializePublish.c \
../w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTSubscribeClient.c \
../w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTSubscribeServer.c \
../w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTUnsubscribeClient.c \
../w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTUnsubscribeServer.c 

OBJS += \
./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTConnectClient.o \
./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTConnectServer.o \
./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTDeserializePublish.o \
./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTFormat.o \
./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTPacket.o \
./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTSerializePublish.o \
./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTSubscribeClient.o \
./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTSubscribeServer.o \
./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTUnsubscribeClient.o \
./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTUnsubscribeServer.o 

C_DEPS += \
./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTConnectClient.d \
./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTConnectServer.d \
./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTDeserializePublish.d \
./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTFormat.d \
./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTPacket.d \
./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTSerializePublish.d \
./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTSubscribeClient.d \
./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTSubscribeServer.d \
./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTUnsubscribeClient.d \
./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTUnsubscribeServer.d 


# Each subdirectory must supply rules for building sources it contributes
w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/%.o w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/%.su w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/%.cyclo: ../w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/%.c w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -c -I../Core/Inc -I../Core/Src/phri -I../w5500/ioLibrary_Driver-master/Internet/SNTP -I../w5500/ioLibrary_Driver-master/Ethernet -I../w5500/ioLibrary_Driver-master/Application/loopback -I../w5500/ioLibrary_Driver-master/Application -I../w5500/ioLibrary_Driver-master/Application/multicast -I../w5500/ioLibrary_Driver-master/Ethernet/W5500 -I../w5500/ioLibrary_Driver-master/Internet/DHCP -I../w5500/ioLibrary_Driver-master/Internet/DNS -I../w5500/ioLibrary_Driver-master/Internet/MQTT -I../w5500/ioLibrary_Driver-master/Internet/httpServer -I../Middlewares/Third_Party/FreeRTOS/Source/include -I../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 -I../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-w5500-2f-ioLibrary_Driver-2d-master-2f-Internet-2f-MQTT-2f-MQTTPacket-2f-src

clean-w5500-2f-ioLibrary_Driver-2d-master-2f-Internet-2f-MQTT-2f-MQTTPacket-2f-src:
	-$(RM) ./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTConnectClient.cyclo ./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTConnectClient.d ./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTConnectClient.o ./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTConnectClient.su ./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTConnectServer.cyclo ./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTConnectServer.d ./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTConnectServer.o ./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTConnectServer.su ./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTDeserializePublish.cyclo ./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTDeserializePublish.d ./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTDeserializePublish.o ./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTDeserializePublish.su ./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTFormat.cyclo ./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTFormat.d ./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTFormat.o ./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTFormat.su ./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTPacket.cyclo ./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTPacket.d ./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTPacket.o ./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTPacket.su ./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTSerializePublish.cyclo ./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTSerializePublish.d ./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTSerializePublish.o ./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTSerializePublish.su ./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTSubscribeClient.cyclo ./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTSubscribeClient.d ./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTSubscribeClient.o ./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTSubscribeClient.su ./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTSubscribeServer.cyclo ./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTSubscribeServer.d ./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTSubscribeServer.o ./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTSubscribeServer.su ./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTUnsubscribeClient.cyclo ./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTUnsubscribeClient.d ./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTUnsubscribeClient.o ./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTUnsubscribeClient.su ./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTUnsubscribeServer.cyclo ./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTUnsubscribeServer.d ./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTUnsubscribeServer.o ./w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/MQTTUnsubscribeServer.su

.PHONY: clean-w5500-2f-ioLibrary_Driver-2d-master-2f-Internet-2f-MQTT-2f-MQTTPacket-2f-src

