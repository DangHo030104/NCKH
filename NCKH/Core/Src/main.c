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
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

#define DHT11_PORT 		GPIOB
#define DHT11_PIN  		GPIO_PIN_14

#define RELAY1_PORT 	GPIOB
#define RELAY1_PIN  	GPIO_PIN_12 	// Van Zone 1

#define RELAY2_PORT 	GPIOB
#define RELAY2_PIN  	GPIO_PIN_13 	// Van Zone 2

#define RELAY3_PORT 	GPIOB
#define RELAY3_PIN  	GPIO_PIN_15 	// Pump

#define LED_PORT 		GPIOC
#define LED_PIN  		GPIO_PIN_13    	// Test Debug

#define RELAY_ON   		GPIO_PIN_RESET
#define RELAY_OFF  		GPIO_PIN_SET

#define LORA_AUX_PORT 	GPIOB
#define LORA_AUX_PIN  	GPIO_PIN_0

#define LORA_M0_PORT 	GPIOB
#define LORA_M0_PIN  	GPIO_PIN_10

#define LORA_M1_PORT 	GPIOB
#define LORA_M1_PIN  	GPIO_PIN_11

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

#define FRAME_TIMEOUT_MS 	1000

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;

TIM_HandleTypeDef htim1;

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */

DHT11_DataTypedef DHT11;

uint16_t temp = 0, humi = 0;
float sm1 = 0, sm2 = 0;  		// Soil Moisture
uint16_t adc_val;        		// ADC default: 12 bit

uint32_t last_readSoil = 0;
uint32_t last_readDHT = 0;
uint32_t last_cmd_time = 0;

char tx_buff[128];
char ack_buff[40];
char rx_buff[128];
uint8_t rx_data;

volatile uint8_t idx = 0;
volatile uint8_t frame_receive = 0;
volatile uint8_t frame_ready = 0;

//======== NODE STATE MACHINE ========

uint32_t wakeStart = 0;
volatile uint8_t lora_wakeup_flag = 0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM1_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_ADC1_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */

static void Pump_Update(void);

static void Process_Frame(char *frame);
static void Process_Request(char *frame);
static void Process_Command(char *cmd);
static uint8_t Wait_For_Frame(uint32_t timeout);

static void send_Data(uint32_t seq, float t, float h, float sm1, float sm2);
static void send_ACK(uint32_t seq);

static uint16_t read_ADC(uint32_t channel);
static void read_Sensors(void);

static void Enter_Stop_Mode(void);

static uint8_t LoRa_WaitReady(uint32_t timeout);
static void LoRa_SetNormalMode(void);
static void LoRa_SetPowerSavingMode(void);

static void Debug_Print(const char *msg);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
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
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */

  DHT11_Init(&DHT11, &htim1, DHT11_PORT, DHT11_PIN);

  HAL_UART_Receive_IT(&huart1, &rx_data, 1);

  Debug_Print("\r\n====================\r\n");
  Debug_Print("STM32 NODE START\r\n");
  Debug_Print("====================\r\n");

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
	    /* =====================================
	     * 1. E32 → POWER-SAVING
	     * ===================================== */

	  	Debug_Print("[POWER] E32 -> POWER SAVING\r\n");
	    LoRa_SetPowerSavingMode();

	    if (!LoRa_WaitReady(100))
	    {
	    	/* Nếu E32 chưa ready, không được STOP */
	        Debug_Print("[ERROR] E32 POWER SAVING NOT READY\r\n");
	        continue;
	    }

	    /* =====================================
	     * 2. STM32 → STOP
	     * ===================================== */

	    Debug_Print("[POWER] Enter STM32 STOP\r\n");
	    Enter_Stop_Mode();


	    /* =====================================
	     * 3. AUX WAKE
	     * ===================================== */

	    if (!lora_wakeup_flag)
	    {
	    	Debug_Print("[WAKE] Wake source NOT LoRa AUX\r\n");
	        continue;
	    }

	    Debug_Print("[AUX] LoRa AUX wake detected\r\n");

	    /* =====================================
	     * 4. WAIT LoRa UART FRAME
	     * (E32 TXD → STM32 USART1)
	     * ===================================== */

	    if (!Wait_For_Frame(FRAME_TIMEOUT_MS))
	    {
	        /* Wake nhưng không nhận đủ frame */
	    	Debug_Print("[ERROR] UART1 FRAME TIMEOUT\r\n");
	        continue;
	    }

	    Debug_Print("[UART1] Frame received: ");
	    Debug_Print(rx_buff);
	    Debug_Print("\r\n");

	    /* =====================================
	     * 5. PROCESS REQ / CMD
	     * ===================================== */

	    frame_ready = 0;
	    Process_Frame(rx_buff);

	    /* Sau đó vòng while chạy lại và STM32 STOP */

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
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

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

/*  HAL_GPIO_WritePin(
      RELAY1_PORT,
      RELAY1_PIN,
      RELAY_OFF
  );

  HAL_GPIO_WritePin(
      RELAY2_PORT,
      RELAY2_PIN,
      RELAY_OFF
  );

  HAL_GPIO_WritePin(
      RELAY3_PORT,
      RELAY3_PIN,
      RELAY_OFF
  );*/

  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10|GPIO_PIN_11|GPIO_PIN_12|GPIO_PIN_13
                          |GPIO_PIN_14|GPIO_PIN_15, GPIO_PIN_RESET);

  /*Configure GPIO pin : PC13 */
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : PB0 */
  GPIO_InitStruct.Pin = GPIO_PIN_0;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : PB10 PB11 PB12 PB13
                           PB14 PB15 */
  GPIO_InitStruct.Pin = GPIO_PIN_10|GPIO_PIN_11|GPIO_PIN_12|GPIO_PIN_13
                          |GPIO_PIN_14|GPIO_PIN_15;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI0_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI0_IRQn);

}

/* USER CODE BEGIN 4 */

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == LORA_AUX_PIN)
    {
        lora_wakeup_flag = 1;
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        char c = (char)rx_data;

        if (c == '<')
        {
            frame_receive = 1;
            idx = 0;
            memset(rx_buff, 0, sizeof(rx_buff));
            rx_buff[idx++] = c;
        }
        else if (frame_receive)
        {
        	if (idx < sizeof(rx_buff) - 1)
            {
            	rx_buff[idx++] = c;

                if (c == '>')
                {
                    rx_buff[idx] = '\0';
                    frame_receive = 0;
                    frame_ready = 1;
                }
            }
            else
            {
                /* Buffer full -> Reset */
                idx = 0;
                frame_receive = 0;
                memset(rx_buff, 0, sizeof(rx_buff));
            }
        }

        // Bật lại interrupt để tiếp tục nhận byte tiếp theo
        HAL_UART_Receive_IT(&huart1, &rx_data, 1);
    }
}

static void Pump_Update(void)
{
    uint8_t zone1 = HAL_GPIO_ReadPin(RELAY1_PORT, RELAY1_PIN);
    uint8_t zone2 = HAL_GPIO_ReadPin(RELAY2_PORT, RELAY2_PIN);

    if (zone1 == RELAY_ON || zone2 == RELAY_ON)
    {
        HAL_GPIO_WritePin(RELAY3_PORT, RELAY3_PIN, RELAY_ON);
    }
    else
    {
        HAL_GPIO_WritePin(RELAY3_PORT, RELAY3_PIN, RELAY_OFF);
    }
}

static void Process_Frame(char *frame)
{
    if (strstr(frame, "<REQ,") != NULL)
    {
        Debug_Print("[FRAME] TYPE = REQ\r\n");
        Process_Request(frame);
        return;
    }

    if (strstr(frame, "<CMD,") != NULL)
    {
        Debug_Print("[FRAME] TYPE = CMD\r\n");
        Process_Command(frame);
        return;
    }

    Debug_Print("[ERROR] UNKNOWN FRAME\r\n");
}

static void Process_Request(char *frame)
{
    unsigned long seq = 0;

    Debug_Print("[REQ] Parse frame: ");
    Debug_Print(frame);
    Debug_Print("\r\n");

    if (sscanf(frame, "<REQ,SEQ=%lu>", &seq) == 1)
    {
    	Debug_Print("[REQ] Valid\r\n");

        /* Chỉ read sensors khi Master yêu cầu */
        Debug_Print("[SENSOR] Start read\r\n");
        read_Sensors();
        Debug_Print("[SENSOR] Read complete\r\n");

        send_Data((uint32_t)seq, temp, humi, sm1, sm2);
    }
    else
    {
        Debug_Print("[ERROR] INVALID REQ\r\n");
    }
}

static void Process_Command(char *cmd)
{
    unsigned long seq = 0;

    Debug_Print("[CMD] Parse frame: ");
    Debug_Print(cmd);
    Debug_Print("\r\n");

    char *seq_ptr = strstr(cmd, "SEQ=");

    if (seq_ptr == NULL)
    {
        Debug_Print("[ERROR] CMD NO SEQ\r\n");
        return;
    }

    if (sscanf(seq_ptr, "SEQ=%lu", &seq) != 1)
    {
        Debug_Print("[ERROR] CMD INVALID SEQ\r\n");
        return;
    }

    Debug_Print("[CMD] Valid\r\n");

    /* =====================================
     * ZONE 1
     * ===================================== */

    if (strstr(cmd, "ZONE=1") != NULL)
    {
    	if (strstr(cmd, "IRR=ON") != NULL)
        {
    		Debug_Print("[RELAY] ZONE1 -> ON\r\n");
            HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(RELAY1_PORT, RELAY1_PIN, RELAY_ON);
            Pump_Update();
        }
        else if (strstr(cmd, "IRR=OFF") != NULL)
        {
        	Debug_Print("[RELAY] ZONE1 -> OFF\r\n");
            HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET);
            HAL_GPIO_WritePin(RELAY1_PORT, RELAY1_PIN, RELAY_OFF);
            Pump_Update();
        }
        else
        {
            return;
        }
    }

    /* =====================================
     * ZONE 2
     * ===================================== */

    else if (strstr(cmd, "ZONE=2") != NULL)
    {
        if (strstr(cmd, "IRR=ON") != NULL)
        {
        	Debug_Print("[RELAY] ZONE2 -> ON\r\n");
            HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(RELAY2_PORT, RELAY2_PIN, RELAY_ON);
            Pump_Update();
        }
        else if (strstr(cmd, "IRR=OFF") != NULL)
        {
        	Debug_Print("[RELAY] ZONE2 -> OFF\r\n");
            HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET);
            HAL_GPIO_WritePin(RELAY2_PORT, RELAY2_PIN, RELAY_OFF);
            Pump_Update();
        }
        else
        {
            return;
        }
    }
    else
    {
        return;
    }

    /* ACK chỉ được gửi sau khi COMMAND đã được xử lý */
    Debug_Print("[CMD] Send ACK\r\n");
    send_ACK((uint32_t)seq);
}

static uint8_t Wait_For_Frame(uint32_t timeout)
{
    uint32_t start = HAL_GetTick();

    while (!frame_ready)
    {
        if (HAL_GetTick() - start >= timeout)
        {
            return 0;
        }
    }

    return 1;
}

static void send_Data(uint32_t seq, float t, float h, float sm1, float sm2)
{
	/* Chuyển E32 từ Power-Saving Mode -> Normal Mode để TX DATA */
	Debug_Print("[LORA] Switch E32 -> NORMAL for Data\r\n");
	LoRa_SetNormalMode();

    if (!LoRa_WaitReady(100))
    {
        Debug_Print("[ERROR] E32 NORMAL NOT READY\r\n");
        return;
    }

    snprintf(tx_buff, sizeof(tx_buff), "<DATA,SEQ=%lu,T=%.2f,H=%.2f,SM1=%.2f,SM2=%.2f>", (unsigned long)seq, t, h, sm1, sm2);

    Debug_Print("[LORA TX] ");
    Debug_Print(tx_buff);
    Debug_Print("\r\n");

    HAL_UART_Transmit(&huart1, (uint8_t *)tx_buff, strlen(tx_buff), 500);

    /* Wait E32 gửi RF xong */
    if (LoRa_WaitReady(500))
    {
        Debug_Print("[LORA TX] RF Transmission Complete\r\n");
    }
    else
    {
        Debug_Print("[ERROR] E32 TX TIMEOUT\r\n");
    }
}

static void send_ACK(uint32_t seq)
{
	Debug_Print("[LORA] Switch E32 -> NORMAL for ACK\r\n");
    LoRa_SetNormalMode();

    if (!LoRa_WaitReady(100))
    {
        Debug_Print("[ERROR] E32 NORMAL NOT READY\r\n");
        return;
    }

    snprintf(ack_buff, sizeof(ack_buff), "<ACK,SEQ=%lu>", (unsigned long)seq);

    Debug_Print("[LORA TX] ");
    Debug_Print(ack_buff);
    Debug_Print("\r\n");

    HAL_UART_Transmit(&huart1, (uint8_t *)ack_buff, strlen(ack_buff), 500);

    if (LoRa_WaitReady(500))
    {
        Debug_Print("[LORA TX] ACK RF COMPLETE\r\n");
    }
    else
    {
        Debug_Print("[ERROR] ACK RF TIMEOUT\r\n");
    }
}

static uint16_t read_ADC(uint32_t channel)
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

static void read_Sensors(void)
{
    /* SOIL MOISTURE */
    sm1 = 100 - ((read_ADC(ADC_CHANNEL_0) / 4095.0) * 100);
    sm2 = 100 - ((read_ADC(ADC_CHANNEL_1) / 4095.0) * 100);

    /* DHT11 */
    if (DHT11_Read_Data(&DHT11))
    {
        temp = DHT11.Temperature;
        humi = DHT11.Humidity;
    }
}

static void Enter_Stop_Mode(void)
{
    lora_wakeup_flag = 0;

    /* Reset parser để tránh còn frame cũ trước STOP */
    idx = 0;
    frame_receive = 0;
    frame_ready = 0;

    memset(rx_buff, 0, sizeof(rx_buff));

    /* Restart UART RX Interrupt */
    HAL_UART_AbortReceive(&huart1);
    HAL_UART_Receive_IT(&huart1, &rx_data, 1);

    /* Clear EXTI pending cũ để tránh vừa vào STOP đã wake */
    __HAL_GPIO_EXTI_CLEAR_IT(LORA_AUX_PIN);

    /* Clear NVIC pending của EXTI0 */
    HAL_NVIC_ClearPendingIRQ(EXTI0_IRQn);

    /* Clear Power Wakeup Flag nếu còn */
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WU);

    /* Suspend SysTick (Tạm dừng) để SysTick không đánh thức MCU liên tục 1ms */
    HAL_SuspendTick();

    /* STOP MODE (WFI = Wait For Interrupt) */
    HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);

    /* STM32 wake -> Restore system clock */
    SystemClock_Config();

    /* Cho HAL_GetTick() chạy lại */
    HAL_ResumeTick();
}

static uint8_t LoRa_WaitReady(uint32_t timeout)
{
    uint32_t start = HAL_GetTick();

    /* Wait E32 Busy(0) -> Ready(1)*/
    while (HAL_GPIO_ReadPin(LORA_AUX_PORT, LORA_AUX_PIN) == GPIO_PIN_RESET)
    {
        if (HAL_GetTick() - start >= timeout)
        {
            return 0;
        }
    }

    return 1;
}

static void LoRa_SetNormalMode(void)
{
    /* M1 = 0, M0 = 0 */
    HAL_GPIO_WritePin(LORA_M1_PORT, LORA_M1_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LORA_M0_PORT, LORA_M0_PIN, GPIO_PIN_RESET);

    HAL_Delay(5);

}

static void LoRa_SetPowerSavingMode(void)
{
    /* M1 = 1, M0 = 0 */
    HAL_GPIO_WritePin(LORA_M1_PORT, LORA_M1_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(LORA_M0_PORT, LORA_M0_PIN, GPIO_PIN_RESET);

    HAL_Delay(5);
}

static void Debug_Print(const char *msg)
{
    HAL_UART_Transmit(&huart2, (uint8_t *)msg, strlen(msg), 100);
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
