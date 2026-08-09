################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

-include ../makefile.init

RM := rm -rf

# All of the sources participating in the build are defined here
-include sources.mk
-include w5500/ioLibrary_Driver-master/Internet/httpServer/subdir.mk
-include w5500/ioLibrary_Driver-master/Internet/TFTP/subdir.mk
-include w5500/ioLibrary_Driver-master/Internet/SNTP/subdir.mk
-include w5500/ioLibrary_Driver-master/Internet/SNMP/subdir.mk
-include w5500/ioLibrary_Driver-master/Internet/MQTT/MQTTPacket/src/subdir.mk
-include w5500/ioLibrary_Driver-master/Internet/MQTT/subdir.mk
-include w5500/ioLibrary_Driver-master/Internet/DNS/subdir.mk
-include w5500/ioLibrary_Driver-master/Internet/DHCP6/subdir.mk
-include w5500/ioLibrary_Driver-master/Internet/DHCP/subdir.mk
-include w5500/ioLibrary_Driver-master/Internet/AAC/subdir.mk
-include w5500/ioLibrary_Driver-master/Ethernet/W5500/subdir.mk
-include w5500/ioLibrary_Driver-master/Ethernet/subdir.mk
-include w5500/ioLibrary_Driver-master/Application/multicast/subdir.mk
-include w5500/ioLibrary_Driver-master/Application/loopback/subdir.mk
-include Middlewares/Third_Party/FreeRTOS/Source/portable/MemMang/subdir.mk
-include Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F/subdir.mk
-include Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2/subdir.mk
-include Middlewares/Third_Party/FreeRTOS/Source/subdir.mk
-include Drivers/STM32H7xx_HAL_Driver/Src/subdir.mk
-include Core/Startup/subdir.mk
-include Core/Src/phri/subdir.mk
-include Core/Src/subdir.mk
-include objects.mk

ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(S_DEPS)),)
-include $(S_DEPS)
endif
ifneq ($(strip $(S_UPPER_DEPS)),)
-include $(S_UPPER_DEPS)
endif
ifneq ($(strip $(C_DEPS)),)
-include $(C_DEPS)
endif
endif

-include ../makefile.defs

OPTIONAL_TOOL_DEPS := \
$(wildcard ../makefile.defs) \
$(wildcard ../makefile.init) \
$(wildcard ../makefile.targets) \


BUILD_ARTIFACT_NAME := Control_board
BUILD_ARTIFACT_EXTENSION := elf
BUILD_ARTIFACT_PREFIX :=
BUILD_ARTIFACT := $(BUILD_ARTIFACT_PREFIX)$(BUILD_ARTIFACT_NAME)$(if $(BUILD_ARTIFACT_EXTENSION),.$(BUILD_ARTIFACT_EXTENSION),)

# Add inputs and outputs from these tool invocations to the build variables 
EXECUTABLES += \
Control_board.elf \

MAP_FILES += \
Control_board.map \

SIZE_OUTPUT += \
default.size.stdout \

OBJDUMP_LIST += \
Control_board.list \


# All Target
all: main-build

# Main-build Target
main-build: Control_board.elf secondary-outputs

# Tool invocations
Control_board.elf Control_board.map: $(OBJS) $(USER_OBJS) D:\2026.05.08\2025_project\2)smarttech\Project\Shelter\2026.05.25_final\MOS_version\control_board\code_mosquitto_choi\STM32H743ZITX_FLASH.ld makefile objects.list $(OPTIONAL_TOOL_DEPS)
	arm-none-eabi-gcc -o "Control_board.elf" @"objects.list" $(USER_OBJS) $(LIBS) -mcpu=cortex-m7 -T"D:\2026.05.08\2025_project\2)smarttech\Project\Shelter\2026.05.25_final\MOS_version\control_board\code_mosquitto_choi\STM32H743ZITX_FLASH.ld" --specs=nosys.specs -Wl,-Map="Control_board.map" -Wl,--gc-sections -static --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -u _printf_float -u _scanf_float -Wl,--start-group -lc -lm -Wl,--end-group
	@echo 'Finished building target: $@'
	@echo ' '

default.size.stdout: $(EXECUTABLES) makefile objects.list $(OPTIONAL_TOOL_DEPS)
	arm-none-eabi-size  $(EXECUTABLES)
	@echo 'Finished building: $@'
	@echo ' '

Control_board.list: $(EXECUTABLES) makefile objects.list $(OPTIONAL_TOOL_DEPS)
	arm-none-eabi-objdump -h -S $(EXECUTABLES) > "Control_board.list"
	@echo 'Finished building: $@'
	@echo ' '

# Other Targets
clean:
	-$(RM) Control_board.elf Control_board.list Control_board.map default.size.stdout
	-@echo ' '

secondary-outputs: $(SIZE_OUTPUT) $(OBJDUMP_LIST)

fail-specified-linker-script-missing:
	@echo 'Error: Cannot find the specified linker script. Check the linker settings in the build configuration.'
	@exit 2

warn-no-linker-script-specified:
	@echo 'Warning: No linker script specified. Check the linker settings in the build configuration.'

.PHONY: all clean dependents main-build fail-specified-linker-script-missing warn-no-linker-script-specified

-include ../makefile.targets
