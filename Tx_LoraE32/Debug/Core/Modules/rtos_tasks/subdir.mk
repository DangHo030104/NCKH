################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (9-2020-q2-update)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Modules/rtos_tasks/app_tasks.c 

OBJS += \
./Core/Modules/rtos_tasks/app_tasks.o 

C_DEPS += \
./Core/Modules/rtos_tasks/app_tasks.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Modules/rtos_tasks/%.o: ../Core/Modules/rtos_tasks/%.c Core/Modules/rtos_tasks/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m3 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F103xB -c -I../Core/Inc -I../Core/Modules/app_common -I../Core/Modules/debug_console -I../Core/Modules/irrigation_control -I../Core/Modules/lora_communication -I../Core/Modules/power_manager -I../Core/Modules/rtos_tasks -I../Core/Modules/sensor_manager -I../Core/Modules/system_manager -I../Core/Modules/telemetry_manager -I../Core/Lib/DHT11 -I../Core/Lib/battery -I../Drivers/STM32F1xx_HAL_Driver/Inc -I../Drivers/STM32F1xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F1xx/Include -I../Drivers/CMSIS/Include -I../Middlewares/Third_Party/FreeRTOS/Source/include -I../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM3 -I../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

