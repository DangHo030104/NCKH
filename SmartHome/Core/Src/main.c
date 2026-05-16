/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

#include "DHT11.h"
#include "ssd1306.h"
#include "ssd1306_fonts.h"
#include "stdio.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

#define RELAY1_FAN_PORT		GPIOB
#define RELAY1_FAN_PIN		GPIO_PIN_0

#define RELAY2_LED1_PORT	GPIOB
#define RELAY2_LED1_PIN		GPIO_PIN_1

#define RELAY3_LED2_PORT	GPIOB
#define RELAY3_LED2_PIN		GPIO_PIN_10

#define TTP223_FAN_PORT     GPIOA
#define TTP223_FAN_PIN      GPIO_PIN_3

#define TTP223_LED1_PORT    GPIOA
#define TTP223_LED1_PIN     GPIO_PIN_4

#define TTP223_LED2_PORT    GPIOA
#define TTP223_LED2_PIN     GPIO_PIN_5

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */



/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

#define DARK_THRESHOLD		2500
#define BRIGHT_THRESHOLD	1500

#define TEMP_ON		30
#define TEMP_OFF 	28

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;

I2C_HandleTypeDef hi2c1;

TIM_HandleTypeDef htim1;

UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */

uint32_t last_readLDR = 0;
uint16_t ldrValue = 0;
uint8_t led1State = 0;

uint32_t last_readDHT = 0;
int temp = 0;
int humi = 0;

uint8_t led2State = 0;

uint8_t fanState = 0;

uint32_t last_oled = 0;

uint8_t radarRxByte;
uint8_t radarBuffer[64];
uint8_t radarIndex = 0;

uint8_t presenceState = 0;
uint8_t movingTarget = 0;
uint8_t staticTarget = 0;

/* MODE: 0: MANUAL, 1: AUTO */
uint8_t fanMode = 1;
uint8_t led1Mode = 1;
uint8_t led2Mode = 1;

/* Lưu trạng thái Touch trước đó để chỉ xử lý khi có thay đổi */
uint8_t lastFanTouchState = 1;
uint8_t lastLed1TouchState = 1;
uint8_t lastLed2TouchState = 1;
/* Debounce Touch */
uint32_t lastFanDebounceTime = 0;
uint32_t lastLed1DebounceTime = 0;
uint32_t lastLed2DebounceTime = 0;
/* Tap Touch Count */
uint32_t lastFanTapTime = 0;
int fanTapCount = 0;

uint32_t lastLed1TapTime = 0;
int led1TapCount = 0;

uint32_t lastLed2TapTime = 0;
int led2TapCount = 0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ADC1_Init(void);
static void MX_TIM1_Init(void);
static void MX_I2C1_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

DHT11_DataTypedef DHT11;

uint16_t Read_LDR(void);

void Parse_LD2410_Frame(uint8_t *buf, uint8_t len);

void control_Touch(void);

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_ADC1_Init();
  MX_TIM1_Init();
  MX_I2C1_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */

  DHT11_Init(&DHT11, &htim1, GPIOA, GPIO_PIN_1);

  HAL_UART_Receive_IT(&huart1, &radarRxByte, 1);	// Start UART interrupt

  // OLED
  ssd1306_Init();

  ssd1306_Fill(Black);	// Clear màn hình

  ssd1306_SetCursor(0, 0);
  ssd1306_WriteString("SMART HOME", Font_11x18, White);		// Vẽ text vào RAM buffer

  ssd1306_SetCursor(0, 24);
  ssd1306_WriteString("Welcome to", Font_7x10, White);

  ssd1306_SetCursor(0, 35);
  ssd1306_WriteString("BLACK HOME!", Font_7x10, White);

  ssd1306_UpdateScreen();

  HAL_Delay(2000);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
	  // Read LDR 1s
	  if(HAL_GetTick() - last_readLDR >= 1000) // 1s
	  {
		  last_readLDR = HAL_GetTick();
		  uint32_t sum = 0;
		  for(int i = 0; i < 10; i++)
		  {
			  sum += Read_LDR();
		  }
		  ldrValue = sum / 10;
	  }

	  // Read DHT11 2s
	  if(HAL_GetTick() - last_readDHT >= 2000) // 2s
	  {
		  last_readDHT = HAL_GetTick();
		  if(DHT11_Read_Data(&DHT11))
		  {
			  temp = DHT11.Temperature;
			  humi = DHT11.Humidity;
		  }
	  }

	  /* ================= MANUAL MODE ================= */


	  // LED1: sân vườn
	  if(led1Mode)
	  {
		  if(ldrValue >= DARK_THRESHOLD)
		  {
		      led1State = 1;
		  }
		  else if(ldrValue <= BRIGHT_THRESHOLD)
		  {
		      led1State = 0;
		  }
	  }

	  HAL_GPIO_WritePin(RELAY2_LED1_PORT, RELAY2_LED1_PIN, led1State ? GPIO_PIN_RESET : GPIO_PIN_SET);

	  // LED2: trong nhà
	  if(led2Mode)
	  {
		  if(ldrValue >= DARK_THRESHOLD && presenceState)
		  {
			  led2State = 1;
		  }
		  else if(ldrValue <= DARK_THRESHOLD || !presenceState)
		  {
			  led2State = 0;
		  }
	  }

	  HAL_GPIO_WritePin(RELAY3_LED2_PORT, RELAY3_LED2_PIN, led2State ? GPIO_PIN_RESET : GPIO_PIN_SET);

	  // Control FAN
	  if(fanMode)
	  {
		  if(temp >= TEMP_ON && presenceState)
		  {
			  fanState = 1;
		  }
		  else if(temp <= TEMP_OFF || !presenceState)
		  {
			  fanState = 0;
		  }
	  }

	  HAL_GPIO_WritePin(RELAY1_FAN_PORT, RELAY1_FAN_PIN, fanState ? GPIO_PIN_RESET : GPIO_PIN_SET);

	  // Control Touch Manual
	  control_Touch();

	  // Update OLED 500ms
	  if (HAL_GetTick() - last_oled >= 500) {
		  last_oled = HAL_GetTick();
		  char buffer[32];

		  ssd1306_Fill(Black);	// Clear màn hình

		  sprintf(buffer, "T: %d*C - H: %d%%", temp, humi);
		  ssd1306_SetCursor(0, 0);
		  ssd1306_WriteString(buffer, Font_7x10, White);

		  sprintf(buffer, "LDR: %d", ldrValue);
		  ssd1306_SetCursor(0, 12);
		  ssd1306_WriteString(buffer, Font_7x10, White);

		  sprintf(buffer, "RADAR:%s", presenceState ? "DETECTED" : "NO DETECT");
		  ssd1306_SetCursor(0, 24);
		  ssd1306_WriteString(buffer, Font_7x10, White);

		  sprintf(buffer, "L1: %s - L2: %s", led1State ? "ON" : "OFF", led2State ? "ON" : "OFF");
		  ssd1306_SetCursor(0, 36);
		  ssd1306_WriteString(buffer, Font_7x10, White);

		  sprintf(buffer, "FAN: %s", fanState ? "ON" : "OFF");
		  ssd1306_SetCursor(0, 48);
		  ssd1306_WriteString(buffer, Font_7x10, White);

		  ssd1306_UpdateScreen();
	  }

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */
  /** Common config
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 1;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }
  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_0;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_71CYCLES_5;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 400000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief TIM1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM1_Init(void)
{

  /* USER CODE BEGIN TIM1_Init 0 */

  /* USER CODE END TIM1_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM1_Init 1 */

  /* USER CODE END TIM1_Init 1 */
  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 72-1;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 65535;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim1, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM1_Init 2 */

  /* USER CODE END TIM1_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 256000;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_10, GPIO_PIN_RESET);

  /*Configure GPIO pin : PA1 */
  GPIO_InitStruct.Pin = GPIO_PIN_1;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : PA3 PA4 PA5 */
  GPIO_InitStruct.Pin = GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_5;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : PB0 PB1 PB10 */
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_10;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

}

/* USER CODE BEGIN 4 */

uint16_t Read_LDR(void)
{
	HAL_ADC_Start(&hadc1);
	HAL_ADC_PollForConversion(&hadc1, 100);
	uint16_t value = HAL_ADC_GetValue(&hadc1);
	HAL_ADC_Stop(&hadc1);

	return value;
}

// Hàm CallBack nhận UART
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if(huart->Instance == USART1)
    {
        radarBuffer[radarIndex++] = radarRxByte;

        if(radarIndex >= sizeof(radarBuffer))
        {
            radarIndex = 0;
        }

        /* LD2410 frame thư�?ng kết thúc bằng F8 F7 F6 F5
        / -> Khi nhận đủ 4 byte cuối trùng sequence này → coi như frame đã hoàn chỉnh */
        if(radarIndex >= 4)
        {
            if(radarBuffer[radarIndex - 4] == 0xF8 &&
               radarBuffer[radarIndex - 3] == 0xF7 &&
               radarBuffer[radarIndex - 2] == 0xF6 &&
               radarBuffer[radarIndex - 1] == 0xF5)
            {
                Parse_LD2410_Frame(radarBuffer, radarIndex);	// Xử lý dữ liệu trong buff
                radarIndex = 0;									// Reset để nhận frame tiếp theo
            }
        }

        HAL_UART_Receive_IT(&huart1, &radarRxByte, 1);	// (De quy)Tiếp tục nhận lại 1 byte dữ liệu->sinh ra ngắt.
    }
}

// Hàm phân tích dữ liệu LD2410C
void Parse_LD2410_Frame(uint8_t *buf, uint8_t len)
{
	/* Frame radar HLK-LD2410C có độ dài tối thiểu ~20 byte */
    if(len < 20) return;	//

    /* Kiểm tra header frame bắt đầu bằng F4 F3 F2 F1 */
    if(buf[0] != 0xF4 || buf[1] != 0xF3 || buf[2] != 0xF2 || buf[3] != 0xF1)
    {
        return;
    }

    /*
     * Dữ liệu cơ bản LD2410 thư�?ng có dạng:
     * Target state:
     * 0x00 = không có nguoi
     * 0x01 = moving target
     * 0x02 = static target
     * 0x03 = moving + static
     */

    uint8_t targetState = buf[8];

    if(targetState == 0x00)
    {
        presenceState = 0;
    }
    else
    {
        presenceState = 1;
    }

    if(targetState == 0x01)
    {
        movingTarget = 1;
    }
    else
    {
    	movingTarget = 0;
    }

    if(targetState == 0x02)
    {
        staticTarget = 1;
    }
    else
    {
    	staticTarget = 0;
    }
}

/* ===== MANUAL MODE ===== */
void control_Touch(void)
{
	uint8_t fanTouchState = HAL_GPIO_ReadPin(TTP223_FAN_PORT, TTP223_FAN_PIN);
	uint8_t led1TouchState = HAL_GPIO_ReadPin(TTP223_LED1_PORT, TTP223_LED1_PIN);
	uint8_t led2TouchState = HAL_GPIO_ReadPin(TTP223_LED2_PORT, TTP223_LED2_PIN);

	// FAN
	if(fanTouchState != lastFanTouchState)
	{
		if(HAL_GetTick() - lastFanDebounceTime >= 100) 	// debounce 100ms
		{
			lastFanDebounceTime = HAL_GetTick();
			if(HAL_GetTick() - lastFanTapTime <= 400)	// double tap
			{
				fanTapCount++;
			}
			else
			{
				fanTapCount = 1;	// Reset
			}

			lastFanTapTime = HAL_GetTick();

			if(fanTapCount == 2)
			{
				fanMode = 1;	// Auto
				fanTapCount = 0;
			}
			else if(fanTapCount == 1)
			{
				fanMode = 0;	// Manual
				fanState = !fanState;
			}
			lastFanTouchState = fanTouchState;
		}
	}

	// LED1
	if(led1TouchState != lastLed1TouchState)
	{
		if(HAL_GetTick() - lastLed1DebounceTime >= 100) 	// debounce 100ms
		{
			lastLed1DebounceTime = HAL_GetTick();
			if(HAL_GetTick() - lastLed1TapTime <= 400)	// double tap
			{
				led1TapCount++;
			}
			else
			{
				led1TapCount = 1;	// Reset
			}

			lastLed1TapTime = HAL_GetTick();

			if(led1TapCount == 2)
			{
				led1Mode = 1;	// Auto
				led1TapCount = 0;
			}
			else if(led1TapCount == 1)
			{
				led1Mode = 0;	// Manual
				led1State = !led1State;
			}
			lastLed1TouchState = led1TouchState;
		}
	}

	// LED2
	if(led2TouchState != lastLed2TouchState)
	{
		if(HAL_GetTick() - lastLed2DebounceTime >= 100) 	// debounce 100ms
		{
			lastLed2DebounceTime = HAL_GetTick();
			if(HAL_GetTick() - lastLed2TapTime <= 400)	// double tap
			{
				led2TapCount++;
			}
			else
			{
				led2TapCount = 1;	// Reset
			}

			lastLed2TapTime = HAL_GetTick();

			if(led2TapCount == 2)
			{
				led2Mode = 1;	// Auto
				led2TapCount = 0;
			}
			else if(led2TapCount == 1)
			{
				led2Mode = 0;	// Manual
				led2State = !led2State;
			}
			lastLed2TouchState = led2TouchState;
		}
	}
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
