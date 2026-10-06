#include "debug_console.h"
#include "system_manager.h"
#include <string.h>

/* ----- Debug Log qua UART2 ----- */

void DebugConsole_Print(const char *msg)
{
    if (msg != NULL)
    {
        HAL_UART_Transmit(&huart2, (uint8_t *)msg, strlen(msg), 100);
    }
}
