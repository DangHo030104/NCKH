/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2025 STMicroelectronics.
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
#include "string.h"
#include "stdio.h"
#include "DHT11.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
#define DHT11_PORT GPIOB
#define DHT11_PIN  GPIO_PIN_14

#define RELAY1_PORT GPIOB
#define RELAY1_PIN  GPIO_PIN_12 // Van Zone 1

#define RELAY2_PORT GPIOB
#define RELAY2_PIN  GPIO_PIN_13 // Van Zone 2

#define RELAY3_PORT GPIOB
#define RELAY3_PIN  GPIO_PIN_15 // Pump

#define LED_PORT GPIOC
#define LED_PIN  GPIO_PIN_13    // Test Debug
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;

TIM_HandleTypeDef htim1;

UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM1_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_ADC1_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

DHT11_DataTypedef DHT11;
/*-------------------*/
// Flag_State: 0: OFF, 1: ON
uint8_t zone1_state = 0;
uint8_t zone2_state = 0;

// Xử lý frame CMD (<CMD,ZONE=1/2,CMD=ON/OFF>)
void handle_frame(char *frame)
{
	HAL_GPIO_TogglePin(LED_PORT, LED_PIN); // Nháy LED mỗi khi có CMD

    if (!strstr(frame, "CMD")) return;  // "CMD" not in frame.

    // "CMD" in frame.
    if (strstr(frame, "ZONE=1"))
    {
        if (strstr(frame, "IRR=ON"))
        {
        	zone1_state = 1;
        	HAL_GPIO_WritePin(RELAY1_PORT, RELAY1_PIN, GPIO_PIN_SET);     // Open van1
        	HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET);		  // LED ON
        }
        else if (strstr(frame, "IRR=OFF"))
        {
        	zone1_state = 0;
            HAL_GPIO_WritePin(RELAY1_PORT, RELAY1_PIN, GPIO_PIN_RESET);   // Close van1 - Không OFF Pump ngay (có thể zone khác đang ON)
            HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET);           // LED OFF
        }
    }
    else if (strstr(frame, "ZONE=2"))
    {
        if (strstr(frame, "IRR=ON"))
        {
        	zone2_state = 1;
        	HAL_GPIO_WritePin(RELAY2_PORT, RELAY2_PIN, GPIO_PIN_SET);     // Open van2
        	HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET);		  // LED ON
        }
        else if (strstr(frame, "IRR=OFF"))
        {
        	zone2_state = 0;
            HAL_GPIO_WritePin(RELAY2_PORT, RELAY2_PIN, GPIO_PIN_RESET);   // Close van2
            HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET);			  // LED OFF
        }
    }

    /* Pump AUTO */
    if(zone1_state == 1 || zone2_state == 1)
    	HAL_GPIO_WritePin(RELAY3_PORT, RELAY3_PIN, GPIO_PIN_SET);   // ON Pump (1 in 2 zone ON)
    else
    	HAL_GPIO_WritePin(RELAY3_PORT, RELAY3_PIN, GPIO_PIN_RESET); // OFF Pump (ALL zone OFF)
}

/*-------------------*/
uint8_t rx_data;          // 1byte.
char rx_buff[50];        // Lưu chuỗi đang nhận
uint8_t rx_index = 0;      // Số byte hiện tại trong rx_buff.
//static uint8_t in_frame = 0;     // Flag State: '0'->not in frame, '1'->đang nhận frame.

// Callback xử lý ngắt nhận
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
	//HAL_GPIO_TogglePin(LED_PORT, LED_PIN);

	if (huart->Instance == USART1)
    {
		char c = (char)rx_data;

	    if (c == '<') {
	        // '<': Bắt đầu frame mới, reset buffer tránh dính dữ liệu cũ.
	        //in_frame = 1;
	        rx_index = 0;
	        //rx_buff[0] = '\0';  // Bỏ byte đầu '<'
	        rx_buff[rx_index++] = c;
	        //HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET);
	    }
	    else if (c == '>' && rx_index < sizeof(rx_buff)-1) {
	        // '>': Kết thúc frame
//	        rx_buff[rx_len] = '\0';  // Bỏ byte cuối '>'
//	        in_frame = 0;

	    	rx_buff[rx_index++] = c;
	    	rx_buff[rx_index] = '\0';  // End chuỗi.

	        handle_frame(rx_buff);   // Xử lý CMD
	        //HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET);
	    }
	    else {
	        // In frame => lưu ký tự
	        if (rx_index < sizeof(rx_buff) - 1) {
	            rx_buff[rx_index++] = c;
	        }
	    }

        HAL_UART_Receive_IT(&huart1, &rx_data, 1);  // (De quy)Tiếp tục nhận lại 1 byte dữ liệu->sinh ra ngắt.
    }
}

/*-------------------*/
char tx_buff[50];

static void send_data(float t, float h, float sm1, float sm2)
{
	sprintf(tx_buff, "<DATA,T=%.2f,H=%.2f,SM1=%.2f,SM2=%.2f>",
			t, h, sm1, sm2);
	HAL_UART_Transmit(&huart1, (uint8_t*)tx_buff, strlen(tx_buff), 200);
}

/*-------------------*/
uint16_t adc_val;     // adc default: 12 bit -> cần sd 2byte=16bit để chứa đủ 12bit.

static uint16_t read_adc(uint32_t channel)
{
    ADC_ChannelConfTypeDef sConfig = {0};

    sConfig.Channel = channel;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_71CYCLES_5;
    HAL_ADC_ConfigChannel(&hadc1, &sConfig);

	HAL_ADC_Start(&hadc1);
	HAL_ADC_PollForConversion(&hadc1, 100);  // Wait ADC Conversion completed.
	adc_val = HAL_ADC_GetValue(&hadc1);
	HAL_ADC_Stop(&hadc1);
	return adc_val;
}

/*-------------------*/
float T = 0; 			 // Temp
float H = 0;    		 // Humi
float SM1 = 0, SM2 = 0;  // Soil Moisture
static uint32_t last_read = 0, last_send = 0;

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
  MX_TIM1_Init();
  MX_USART1_UART_Init();
  MX_ADC1_Init();
  /* USER CODE BEGIN 2 */
  //HAL_TIM_Base_Start(&htim1);   // Start Timer 1.

  DHT11_Init(&DHT11, &htim1, DHT11_PORT, DHT11_PIN);

  HAL_UART_Receive_IT(&huart1, &rx_data, 1);  // Nhận được 1 byte sinh ra ngắt(*)-> Nhảy vào hàm ngắt nhận.

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
	  uint32_t now = HAL_GetTick();

	  // Read cảm biến mỗi 1s
	  if(now - last_read >= 1000){
		  last_read = now;
		  /* Soil Moisture Sensor */
		  SM1 = 100 - ((read_adc(ADC_CHANNEL_0) / 4095.0) * 100);  // Convert adc to %.
		  SM2 = 100 - ((read_adc(ADC_CHANNEL_1) / 4095.0) * 100);
		  /* DHT11 Sensor*/
		  if(DHT11_Read_Data(&DHT11))
		  {
			  T = DHT11.Temperature;
			  H = DHT11.Humidity;
		  }
	  }

	  // Gửi dữ liệu mỗi 2s
	  if (now - last_send >= 2000)
	  {
		  last_send = now;
		  send_data(T, H, SM1, SM2);
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
  hadc1.Init.ContinuousConvMode = ENABLE;
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
  sConfig.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

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
  htim1.Init.Prescaler = 71;
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
  huart1.Init.BaudRate = 9600;
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
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12|GPIO_PIN_13|DHT11_Pin|GPIO_PIN_15, GPIO_PIN_RESET);

  /*Configure GPIO pin : PC13 */
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : PB12 PB13 DHT11_Pin PB15 */
  GPIO_InitStruct.Pin = GPIO_PIN_12|GPIO_PIN_13|DHT11_Pin|GPIO_PIN_15;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

}

/* USER CODE BEGIN 4 */

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
