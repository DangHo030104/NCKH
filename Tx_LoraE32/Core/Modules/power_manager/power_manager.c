#include "power_manager.h"
#include "app_config.h"
#include "system_manager.h"

/* ----- Quyết định khi nào STM32 được vào STOP -----*/

extern void SystemClock_Config(void);

uint8_t PowerManager_CanEnterStop(void)
{
#if DEBUG_NO_STOP
    return 0;
#endif
    /* Chỉ được STOP khi toàn bộ hệ thống đang IDLE. */

    /* 1. AUTO chỉ được ngủ ở trạng thái IDLE hoặc FAILED. */
    if (irr_mode == MODE_AUTO && irr_state != IRR_IDLE && irr_state != IRR_FAILED) return 0;

    /* 2. Không ngủ khi relay có owner hoặc đang chờ SensorTask đo cho AUTO. */
    if (relay_owner != RELAY_OWNER_NONE || irr_measure_pending) return 0;

    /* 3. Phải gửi xong telemetry trạng thái cuối trước khi ngủ. */
    if (telemetry_event_pending || telemetry_sensor_pending || telemetry_ready) return 0;

    /* 4. Không ngủ khi UART đang nhận hoặc còn frame chưa xử lý. */
    if (frame_receive || frame_ready) return 0;

    /* 5. AUX Low -> LoRaE32 đang bận TX/RX. */
    if (HAL_GPIO_ReadPin(LORA_AUX_PORT, LORA_AUX_PIN) == GPIO_PIN_RESET) return 0;

    return 1;
}

void PowerManager_EnterStop(void)
{
    /* 1. Atomic final check: khóa IRQ để tránh bỏ lỡ sự kiện ngay trước WFI. */
    __disable_irq();

    if (lora_wakeup_flag || rtc_wakeup_flag || frame_ready || frame_receive ||
        __HAL_UART_GET_FLAG(&huart1, UART_FLAG_RXNE))
    {
        __enable_irq();
        return;
    }

    /* 2. Xóa các cờ wake cũ để tránh vừa vào STOP đã thức dậy. */
    __HAL_GPIO_EXTI_CLEAR_IT(LORA_AUX_PIN);
    HAL_NVIC_ClearPendingIRQ(EXTI15_10_IRQn);
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WU);

    /* 3. Dừng HAL timebase (TIM2) và FreeRTOS SysTick trước khi STOP. */
    HAL_SuspendTick();
    SysTick->CTRL &= ~SysTick_CTRL_TICKINT_Msk;
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;

    /* 4. Vào STOP mode bằng WFI. */
    HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);

    /* 5. Sau khi wake: khôi phục clock và tick. */
    SystemClock_Config();
    HAL_ResumeTick();
    SysTick->CTRL |= SysTick_CTRL_TICKINT_Msk;

    /* Khi enable IRQ, EXTI AUX đang pending sẽ set lora_wakeup_flag */
    __enable_irq();
}

void HAL_GPIO_EXTI_Callback(uint16_t pin)
{
    /* AUX falling edge báo E32 đánh thức STM32 để nhận frame LoRa. */
    if (pin == LORA_AUX_PIN) lora_wakeup_flag = 1;
}
