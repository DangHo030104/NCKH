#include "lora_e32.h"
#include "app_config.h"
#include "system_manager.h"
#include "cmsis_os2.h"
#include <string.h>

/* ----- Giao tiếp với LoRaE32 ----- */

static char isr_buffer[128];
static uint8_t rx_byte;
static volatile uint8_t rx_index = 0;

void LoRaE32_Init(void)
{
    rx_index = 0;
    frame_receive = 0;
    frame_ready = 0;
    memset(isr_buffer, 0, sizeof(isr_buffer));
    HAL_UART_Receive_IT(&huart1, &rx_byte, 1U);
}

uint8_t LoRaE32_WaitReady(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();

    /* Chờ AUX chuyển từ BUSY (0) sang READY (1). */
    while (HAL_GPIO_ReadPin(LORA_AUX_PORT, LORA_AUX_PIN) == GPIO_PIN_RESET)
    {
        if ((HAL_GetTick() - start) >= timeout_ms) return 0;

        /* Nhường 1ms CPU cho task khác, tránh busy-wait khi E32 đang bận. */
        osDelay(1);
    }
    return 1;
}

uint8_t LoRaE32_WaitForFrame(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    while (!frame_ready)
    {
        if ((HAL_GetTick() - start) >= timeout_ms) return 0;

        osDelay(1); /* Nhường CPU cho task khác trong lúc chờ ISR nhận đủ frame. */
    }
    return 1;
}

void LoRaE32_SetNormalMode(void)
{
    /* Normal mode: M1 = 0, M0 = 0. */
    HAL_GPIO_WritePin(LORA_M1_PORT, LORA_M1_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LORA_M0_PORT, LORA_M0_PIN, GPIO_PIN_RESET);

    osDelay(5);
}

void LoRaE32_SetPowerSavingMode(void)
{
    /* Power-saving mode: M1 = 1, M0 = 0. */
    HAL_GPIO_WritePin(LORA_M1_PORT, LORA_M1_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(LORA_M0_PORT, LORA_M0_PIN, GPIO_PIN_RESET);

    osDelay(5);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart)
{
    if (uart->Instance != USART1) return;

    char c = (char)rx_byte;
    last_lora_rx_tick = HAL_GetTick();

    if (c == '<')
    {
        /* Ký tự bắt đầu frame: reset buffer ISR và bắt đầu thu frame mới. */
        frame_receive = 1;
        rx_index = 0;
        memset(isr_buffer, 0, sizeof(isr_buffer));
        isr_buffer[rx_index++] = c;
    }
    else if (frame_receive)
    {
        if (rx_index < sizeof(isr_buffer) - 1U)
        {
            isr_buffer[rx_index++] = c;
            if (c == '>')
            {
                /* Chỉ publish frame mới khi LoRaTask đã xử lý frame trước. */
                isr_buffer[rx_index] = '\0';
                frame_receive = 0;
                if (!frame_ready)
                {
                    /* Chỉ copy đúng số byte vừa nhận, kể cả ký tự kết thúc. */
                    memcpy(rx_frame_buffer, isr_buffer, rx_index + 1U);
                    frame_ready = 1;
                }
                else
                {
                    /* Frame cũ chưa xử lý: không ghi đè rx_frame_buffer. */
                    rx_frame_drop_count++;
                }
                rx_index = 0;
            }
        }
        else
        {
            /* Buffer đầy nhưng chưa có ký tự kết thúc: hủy frame lỗi. */
            rx_index = 0;
            frame_receive = 0;
            memset(isr_buffer, 0, sizeof(isr_buffer));
        }
    }

    /* Bật lại UART interrupt để nhận byte tiếp theo. */
    HAL_UART_Receive_IT(&huart1, &rx_byte, 1U);
}
