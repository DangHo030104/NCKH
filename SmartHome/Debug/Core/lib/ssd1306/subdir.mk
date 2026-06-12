################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (9-2020-q2-update)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/lib/ssd1306/ssd1306.c \
../Core/lib/ssd1306/ssd1306_fonts.c \
../Core/lib/ssd1306/ssd1306_tests.c 

OBJS += \
./Core/lib/ssd1306/ssd1306.o \
./Core/lib/ssd1306/ssd1306_fonts.o \
./Core/lib/ssd1306/ssd1306_tests.o 

C_DEPS += \
./Core/lib/ssd1306/ssd1306.d \
./Core/lib/ssd1306/ssd1306_fonts.d \
./Core/lib/ssd1306/ssd1306_tests.d 


# Each subdirectory must supply rules for building sources it contributes
Core/lib/ssd1306/%.o: ../Core/lib/ssd1306/%.c Core/lib/ssd1306/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m3 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F103xB -c -I"D:/STM32CUBEIDE/SmartHome/Core/Modules/device_control" -I"D:/STM32CUBEIDE/SmartHome/Core/lib/DHT11" -I"D:/STM32CUBEIDE/SmartHome/Core/lib/ssd1306" -I"D:/STM32CUBEIDE/SmartHome/Core/Modules/pc_serial_com" -I"D:/STM32CUBEIDE/SmartHome/Core/Modules/sensor_manager" -I"D:/STM32CUBEIDE/SmartHome/Core/Modules/smart_home_system" -I"D:/STM32CUBEIDE/SmartHome/Core/Modules/touch_button" -I"D:/STM32CUBEIDE/SmartHome/Core/Modules/user_interface" -I../Core/Inc -I../Drivers/STM32F1xx_HAL_Driver/Inc -I../Drivers/STM32F1xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F1xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

