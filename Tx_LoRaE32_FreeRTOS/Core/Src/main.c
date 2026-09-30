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
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

#include "string.h"
#include "stdio.h"
#include "DHT11.h"
#include "battery.h"

#include "FreeRTOS.h"
#include "task.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* ===== STATE MACHINE ===== */
typedef enum
{
    MODE_MANUAL = 0,
    MODE_AUTO
} IrrigationMode;

typedef enum
{
    VALVE_OFF = 0,
    VALVE_ON
} ValveState;

typedef enum
{
    IRR_IDLE = 0,

    IRR_ZONE1_WATERING,
    IRR_ZONE1_SOAK,
	IRR_ZONE1_MEASURE,

    IRR_ZONE2_WATERING,
    IRR_ZONE2_SOAK,
	IRR_ZONE2_MEASURE
} IrrigationState;

typedef enum
{
    RELAY_OWNER_NONE = 0,
    RELAY_OWNER_MANUAL,
    RELAY_OWNER_AUTO
}RelayOwner;

typedef enum
{
    WAKE_NONE = 0,
    WAKE_RTC,
    WAKE_LORA
} WakeSource;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

#define DHT11_PORT 			GPIOB
#define DHT11_PIN  			GPIO_PIN_14

#define RELAY1_PORT 		GPIOA
#define RELAY1_PIN  		GPIO_PIN_5 		// Van Zone 1

#define RELAY2_PORT 		GPIOA
#define RELAY2_PIN  		GPIO_PIN_6 		// Van Zone 2

#define RELAY3_PORT 		GPIOA
#define RELAY3_PIN  		GPIO_PIN_7 		// Pump

#define LED_PORT 			GPIOC
#define LED_PIN  			GPIO_PIN_13    	// Test Debug

#define RELAY_ON   			GPIO_PIN_SET
#define RELAY_OFF  			GPIO_PIN_RESET

#define LORA_AUX_PORT 		GPIOA
#define LORA_AUX_PIN  		GPIO_PIN_15

#define LORA_M0_PORT 		GPIOA
#define LORA_M0_PIN  		GPIO_PIN_11

#define LORA_M1_PORT 		GPIOA
#define LORA_M1_PIN  		GPIO_PIN_12

#define BATTERY_ADC_CHANNEL ADC_CHANNEL_4   // PA4

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

#define FRAME_TIMEOUT_MS			1000
#define CMD_DUPLICATE_MS 			30000U  	// ESP32 retry window: avoid executing the same CMD twice
#define MANUAL_WATER_TIMEOUT_MS 	60000U  	// Safety: auto-stop manual watering after 60 s
#define AUTO_SENSOR_INTERVAL_MS    	5000U   	// AUTO đo lại sensor mỗi 60 giây

/* Mỗi signal dùng một bit riêng */
#define SENSOR_READ_SIGNAL     	0x01		// 0x01 → LoRaTask yêu cầu SensorTask read sensors
#define SENSOR_READY_SIGNAL    	0x02		// 0x02 → SensorTask báo LoRaTask sensor Ready

#define CONTROL_EXEC_SIGNAL  	0x04		// 0x04 → Execute control
#define CONTROL_OK_SIGNAL     	0x08
#define CONTROL_ERROR_SIGNAL  	0x10

#define IRRIGATION_READ_SIGNAL  0x20
#define IRRIGATION_READY_SIGNAL 0x40

/* (Kham khảo) Giá trị tạm th�?i, sẽ đo lại thực tế ở 2 khu đất */
#define SOIL1_ADC_DRY   3000
#define SOIL1_ADC_WET   1500

#define SOIL2_ADC_DRY   3000
#define SOIL2_ADC_WET   1500

/* (Kham khảo) Logic: SM < 35% → cần tưới | SM >= 55% → đủ nước
 * Khoảng: 35% → 55% là vùng hysteresis để tránh không ON/OFF Relay liên tục quanh 1 mức */
#define SOIL_START_THRESHOLD    35.0f
#define SOIL_STOP_THRESHOLD     55.0f

/* (Kham khảo) Tưới theo xung:
 * Tưới      : 2 giây
 * Nghỉ thấm : 10 giây
 * Tối đa    : 5 chu kỳ */
#define WATER_PULSE_MS      	2000
#define SOAK_TIME_MS       		10000
#define MAX_IRRIGATION_CYCLE 	5

/* RTC Low Power Wakeup Interval (60s) */
#define RTC_WAKEUP_INTERVAL_SEC   10U

/* Debug mode (1): disable STOP mode while using CubeIDE debugger */
#define DEBUG_NO_STOP   0

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;

RTC_HandleTypeDef hrtc;

TIM_HandleTypeDef htim1;

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;

osThreadId LoRaTaskHandle;
osThreadId SensorTaskHandle;
osThreadId IrrigationTaskHandle;
/* USER CODE BEGIN PV */

DHT11_DataTypedef DHT11;

volatile uint16_t temp = 0, humi = 0;
uint16_t soil1_adc, soil2_adc, adc_val;			// ADC default: 12 bit
volatile float sm1 = 0, sm2 = 0;  				// Soil Moisture
volatile uint8_t sensor_data_valid = 0;
volatile uint32_t last_sensor_update_tick = 0;	// Th�?i điểm gần nhất SensorTask đ�?c sensor thành công

/* Battery */
volatile float battery_voltage = 0.0f;
volatile uint8_t battery_percent = 0;

uint32_t last_cmd_time = 0;

char tx_buff[128];
char ack_buff[128];
char rx_ISR_buffer[128];
char rx_frame_buffer[128];
uint8_t rx_data;

volatile uint8_t idx = 0;
volatile uint8_t frame_receive = 0;
volatile uint8_t frame_ready = 0;

/* Count frame drop nếu frame cũ chưa xử lý xong */
volatile uint32_t rx_frame_drop_count = 0;

volatile uint8_t lora_wakeup_flag = 0;

char control_command[64];

/* Duplicate CMD protection: remember the last successfully executed command. */
uint32_t last_cmd_seq = 0;
uint32_t last_cmd_tick = 0;
uint8_t last_cmd_valid = 0;
char last_cmd[64] = {0};

char dbg[80];

volatile IrrigationMode irr_mode = MODE_AUTO;
volatile ValveState valve1_state = VALVE_OFF;
volatile ValveState valve2_state = VALVE_OFF;
volatile IrrigationState irr_state = IRR_IDLE;
volatile RelayOwner relay_owner = RELAY_OWNER_NONE;
volatile WakeSource wake_source = WAKE_NONE;

uint32_t irr_state_start = 0;
volatile uint8_t irr_measure_pending = 0;
uint8_t zone1_cycle = 0, zone2_cycle = 0;

volatile uint8_t rtc_wakeup_flag = 0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM1_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_ADC1_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_RTC_Init(void);
void StartLoRaTask(void const * argument);
void StartSensorTask(void const * argument);
void StartIrrigationTask(void const * argument);

/* USER CODE BEGIN PFP */

static void StartZone1(void);
static void StartZone2(void);
static void StopAllIrrigation(void);
static void StopCurrentZone(RelayOwner owner);
static void Irrigation_AutoUpdate(void);

static void Process_Frame(char *frame);
static void Process_Request(char *frame);
static void Process_Command(char *cmd);
static uint8_t Wait_For_Frame(uint32_t timeout);

static void send_Data(uint32_t seq);
static void send_ACK(uint32_t seq);

static uint16_t read_ADC(uint32_t channel);
static float Soil_ADC_ToPercent(uint16_t adc, uint16_t adc_dry, uint16_t adc_wet);
static void read_Sensors(void);
static void read_Battery(void);

static uint8_t Can_Enter_Stop_Mode(void);
static void Enter_Stop_Mode(void);

static uint8_t LoRa_WaitReady(uint32_t timeout);
static void LoRa_SetNormalMode(void);
static void LoRa_SetPowerSavingMode(void);

static void Debug_Print(const char *msg);

static void Irrigation_Measure(void);
static void Relay_Manual(void);
static void Relay_Auto(void);
static void Relay_Release(void);

void RTC_Print_Time(void);
void RTC_SetAlarmAfterSeconds(uint8_t seconds);

static uint8_t Process_Control_Command(void);
static void Manual_Water_Safety_Check(void);

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
  MX_RTC_Init();
  /* USER CODE BEGIN 2 */

  DHT11_Init(&DHT11, &htim1, DHT11_PORT, DHT11_PIN);

  Battery_Init(&hadc1);

  HAL_UART_Receive_IT(&huart1, &rx_data, 1);

  Debug_Print("\r\n====================\r\n");
  Debug_Print("STM32 NODE START\r\n");
  Debug_Print("====================\r\n");

  /* USER CODE END 2 */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* definition and creation of LoRaTask */
  osThreadDef(LoRaTask, StartLoRaTask, osPriorityNormal, 0, 512);
  LoRaTaskHandle = osThreadCreate(osThread(LoRaTask), NULL);

  /* definition and creation of SensorTask */
  osThreadDef(SensorTask, StartSensorTask, osPriorityNormal, 0, 256);
  SensorTaskHandle = osThreadCreate(osThread(SensorTask), NULL);

  /* definition and creation of IrrigationTask */
  osThreadDef(IrrigationTask, StartIrrigationTask, osPriorityNormal, 0, 512);
  IrrigationTaskHandle = osThreadCreate(osThread(IrrigationTask), NULL);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */
  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
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
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE|RCC_OSCILLATORTYPE_LSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
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
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_RTC|RCC_PERIPHCLK_ADC;
  PeriphClkInit.RTCClockSelection = RCC_RTCCLKSOURCE_LSE;
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
  * @brief RTC Initialization Function
  * @param None
  * @retval None
  */
static void MX_RTC_Init(void)
{

  /* USER CODE BEGIN RTC_Init 0 */

  /* USER CODE END RTC_Init 0 */

  RTC_TimeTypeDef sTime = {0};
  RTC_DateTypeDef DateToUpdate = {0};

  /* USER CODE BEGIN RTC_Init 1 */

  /* USER CODE END RTC_Init 1 */
  /** Initialize RTC Only
  */
  hrtc.Instance = RTC;
  hrtc.Init.AsynchPrediv = RTC_AUTO_1_SECOND;
  hrtc.Init.OutPut = RTC_OUTPUTSOURCE_NONE;
  if (HAL_RTC_Init(&hrtc) != HAL_OK)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN Check_RTC_BKUP */

  /* USER CODE END Check_RTC_BKUP */

  /** Initialize RTC and set the Time and Date
  */
  sTime.Hours = 0;
  sTime.Minutes = 0;
  sTime.Seconds = 0;

  if (HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN) != HAL_OK)
  {
    Error_Handler();
  }
  DateToUpdate.WeekDay = RTC_WEEKDAY_MONDAY;
  DateToUpdate.Month = RTC_MONTH_JANUARY;
  DateToUpdate.Date = 1;
  DateToUpdate.Year = 26;

  if (HAL_RTC_SetDate(&hrtc, &DateToUpdate, RTC_FORMAT_BIN) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN RTC_Init 2 */

  /* USER CODE END RTC_Init 2 */

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
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_7|GPIO_PIN_11
                          |GPIO_PIN_12, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_RESET);

  /*Configure GPIO pin : PC13 */
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : PA5 PA6 PA7 PA11
                           PA12 */
  GPIO_InitStruct.Pin = GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_7|GPIO_PIN_11
                          |GPIO_PIN_12;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : PB14 */
  GPIO_InitStruct.Pin = GPIO_PIN_14;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : PA15 */
  GPIO_InitStruct.Pin = GPIO_PIN_15;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

}

/* USER CODE BEGIN 4 */

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == LORA_AUX_PIN)
    {
        lora_wakeup_flag = 1;
    }
}

void HAL_RTC_AlarmAEventCallback(RTC_HandleTypeDef *hrtc)
{
	rtc_wakeup_flag = 1;
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
            memset(rx_ISR_buffer, 0, sizeof(rx_ISR_buffer));
            rx_ISR_buffer[idx++] = c;
        }
        else if (frame_receive)
        {
        	if (idx < sizeof(rx_ISR_buffer) - 1)
            {
        		rx_ISR_buffer[idx++] = c;

                if (c == '>')
                {
                	rx_ISR_buffer[idx] = '\0';
                    frame_receive = 0;

                    /* Chỉ publish frame mới khi LoRaTask đã xử lý frame trước */
                    if (frame_ready == 0)
                    {
                        memcpy(rx_frame_buffer, rx_ISR_buffer, idx + 1);	// Chỉ copy đúng số byte đã nhận
                        frame_ready = 1;
                    }
                    else
                    {
                        /* Frame cũ vẫn chưa xử lý -> Không overwrite rx_frame_buffer. */
                        rx_frame_drop_count++;
                    }

                    idx = 0;
                }
            }
            else
            {
                /* Buffer full -> Reset */
                idx = 0;
                frame_receive = 0;
                memset(rx_ISR_buffer, 0, sizeof(rx_ISR_buffer));
            }
        }

        // Bật lại UART ISR để tiếp tục nhận byte tiếp theo
        HAL_UART_Receive_IT(&huart1, &rx_data, 1);
    }
}

/* ===== Irrigation control functions ===== */

static void StartZone1(void)
{
	Debug_Print("[IRR] Start Zone1\r\n");

    /* Van Zone2 OFF */
    HAL_GPIO_WritePin(RELAY2_PORT, RELAY2_PIN, RELAY_OFF);

    /* Van Zone1 ON */
    HAL_GPIO_WritePin(RELAY1_PORT, RELAY1_PIN, RELAY_ON);

    valve1_state = VALVE_ON;
    valve2_state = VALVE_OFF;

    /* Wait van mở ổn định */
    osDelay(300);

    /* Bật Pump */
    HAL_GPIO_WritePin(RELAY3_PORT, RELAY3_PIN, RELAY_ON);
}

static void StartZone2(void)
{
	Debug_Print("[IRR] Start Zone2\r\n");

	/* Van Zone 1 OFF */
    HAL_GPIO_WritePin(RELAY1_PORT, RELAY1_PIN, RELAY_OFF);

    /* Van Zone2 ON */
    HAL_GPIO_WritePin(RELAY2_PORT, RELAY2_PIN, RELAY_ON);

    valve1_state = VALVE_OFF;
    valve2_state = VALVE_ON;

    /* Wait van mở ổn định */
    osDelay(300);

    /* Bật Pump */
    HAL_GPIO_WritePin(RELAY3_PORT, RELAY3_PIN, RELAY_ON);
}

static void StopAllIrrigation(void)
{
	Debug_Print("[IRR] Stop all irrigation\r\n");
    /* Pump OFF trước */
    HAL_GPIO_WritePin(RELAY3_PORT, RELAY3_PIN, RELAY_OFF);

    osDelay(300);

    /* Sau đó đóng cả 2 van */
    HAL_GPIO_WritePin(RELAY1_PORT, RELAY1_PIN, RELAY_OFF);
    HAL_GPIO_WritePin(RELAY2_PORT, RELAY2_PIN, RELAY_OFF);

    /* �?ồng bộ trạng thái phần m�?m với trạng thái relay thực tế.
     * Nếu không cập nhật hai biến này, DATA gửi lên ESP32/Web có thể vẫn báo van ON dù relay đã OFF */
    valve1_state = VALVE_OFF;
    valve2_state = VALVE_OFF;
}

static void StopCurrentZone(RelayOwner owner)
{
	/* �?ể tránh trư�?ng hợp AUTO can thiệp vào khi MANNUAL đang sở hữu Relay */
    if(owner != relay_owner)
    {
        Debug_Print("[RELAY] Stop rejected\r\n");
        return;
    }

	Debug_Print("[IRR] Stop current zone\r\n");
	StopAllIrrigation();
}

static void Irrigation_AutoUpdate(void)
{
    if(relay_owner == RELAY_OWNER_MANUAL) return;

    if (irr_mode != MODE_AUTO) return;

    /* AUTO không được quyết định tưới khi chưa có dữ liệu sensor (STM32 reboot) */
    if (!sensor_data_valid)
    {
        if (!irr_measure_pending)
        {
        	Debug_Print("[AUTO] Sensor data invalid -> request measurement\r\n");

            Irrigation_Measure();
        }

        return;
    }

    /* AUTO IDLE: lấy dữ liệu sensor mới định kỳ */
    if (irr_state == IRR_IDLE && !irr_measure_pending && (HAL_GetTick() - last_sensor_update_tick) >= AUTO_SENSOR_INTERVAL_MS)
    {
        Debug_Print("[AUTO] Periodic sensor measurement\r\n");

        /* Không cho AUTO sử dụng dữ liệu cũ để quyết định tưới */
        sensor_data_valid = 0;

        Irrigation_Measure();

        return;
    }

    switch (irr_state)
    {
        /* =====================================
         * IDLE - CHỌN ZONE CẦN TƯỚI
         * ===================================== */
        case IRR_IDLE:
        {
            uint8_t zone1 = (sm1 < SOIL_START_THRESHOLD);
            uint8_t zone2 = (sm2 < SOIL_START_THRESHOLD);

            /* Không Zone nào khô -> �?ủ ẩm */
            if (!zone1 && !zone2)
            {
                /* Nếu hệ thống thực sự đã OFF hoàn toàn thì không g�?i lại StopAllIrrigation() để tránh block 300ms lặp liên tục */
                if (relay_owner != RELAY_OWNER_NONE || valve1_state != VALVE_OFF || valve2_state != VALVE_OFF)
                {
                    Debug_Print("[AUTO] IDLE -> ensure irrigation OFF\r\n");

                    StopAllIrrigation();
                    Relay_Release();
                }

                return;
            }

            /* Chỉ Zone1 khô */
            if (zone1 && !zone2)
            {
                Debug_Print("[AUTO] Select Zone1\r\n");

                Relay_Auto();

                irr_state = IRR_ZONE1_WATERING;
                irr_state_start = HAL_GetTick();

                StartZone1();

                return;
            }

            /* Chỉ Zone2 khô */
            if (!zone1 && zone2)
            {
                Debug_Print("[AUTO] Select Zone2\r\n");

                Relay_Auto();

                irr_state = IRR_ZONE2_WATERING;
                irr_state_start = HAL_GetTick();

                StartZone2();

                return;
            }

            /* Cả hai Zone khô (cần tưới)
             * -> Zone khô hơn được ưu tiên */
            if (zone1 && zone2)
            {
                if (sm1 <= sm2)
                {
                    Debug_Print("[AUTO] Both dry -> Zone1 first\r\n");

                    Relay_Auto();

                    irr_state = IRR_ZONE1_WATERING;
                    irr_state_start = HAL_GetTick();

                    StartZone1();
                }
                else
                {
                    Debug_Print("[AUTO] Both dry -> Zone2 first\r\n");

                    Relay_Auto();

                    irr_state = IRR_ZONE2_WATERING;
                    irr_state_start = HAL_GetTick();

                    StartZone2();
                }

                return;
            }

            break;
        }

        case IRR_ZONE1_WATERING:
        {
            if (HAL_GetTick() - irr_state_start >= WATER_PULSE_MS)
            {
                Debug_Print("[AUTO] Zone1 pulse complete\r\n");

                StopCurrentZone(RELAY_OWNER_AUTO);

                irr_state = IRR_ZONE1_SOAK;

                irr_state_start = HAL_GetTick();
            }

            break;
        }

        case IRR_ZONE1_SOAK:
        {
            if (HAL_GetTick() - irr_state_start >= SOAK_TIME_MS)
            {
                Debug_Print("[AUTO] Zone1 soak complete\r\n");

                Irrigation_Measure();

                if (irr_measure_pending)
                {
                    irr_state = IRR_ZONE1_MEASURE;
                }
            }

            break;
        }

        case IRR_ZONE1_MEASURE:
        {
            /* Wait SensorTask read */
            if(irr_measure_pending == 0)
            {
                if(sm1 >= SOIL_STOP_THRESHOLD)
                {
                    Debug_Print("[AUTO] Zone1 moisture OK\r\n");

                    zone1_cycle = 0;

                    Relay_Release();
                    irr_state = IRR_IDLE;
                }
                else
                {
                    Debug_Print("[AUTO] Zone1 still dry\r\n");

                    zone1_cycle++;

                    if(zone1_cycle >= MAX_IRRIGATION_CYCLE)
                    {
                        Debug_Print("[ERROR] Zone1 irrigation failed\r\n");

                        Relay_Release();
                        zone1_cycle = 0;
                        irr_state = IRR_IDLE;

                        break;
                    }

                    irr_state = IRR_ZONE1_WATERING;
                    irr_state_start = HAL_GetTick();

                    StartZone1();
                }

            }

            break;

        }

        case IRR_ZONE2_WATERING:
        {
            if (HAL_GetTick() - irr_state_start >= WATER_PULSE_MS)
            {
                Debug_Print("[AUTO] Zone2 pulse complete\r\n");

                StopCurrentZone(RELAY_OWNER_AUTO);
                irr_state = IRR_ZONE2_SOAK;
                irr_state_start = HAL_GetTick();
            }

            break;
        }

        case IRR_ZONE2_SOAK:
        {
            if (HAL_GetTick() - irr_state_start >= SOAK_TIME_MS)
            {
                Debug_Print("[AUTO] Zone2 soak complete\r\n");

                Irrigation_Measure();

                if (irr_measure_pending)
                {
                    irr_state = IRR_ZONE2_MEASURE;
                }
            }

            break;
        }

        case IRR_ZONE2_MEASURE:
        {
            /* Wait SensorTask read xong */
            if(irr_measure_pending == 0)
            {
                if(sm2 >= SOIL_STOP_THRESHOLD)
                {
                    Debug_Print("[AUTO] Zone2 moisture OK\r\n");

                    zone2_cycle = 0;

                    Relay_Release();
                    irr_state = IRR_IDLE;
                }
                else
                {
                    Debug_Print("[AUTO] Zone2 still dry\r\n");

                    zone2_cycle++;

                    if(zone2_cycle >= MAX_IRRIGATION_CYCLE)
                    {
                        Debug_Print("[ERROR] Zone2 irrigation failed\r\n");

                        Relay_Release();
                        zone2_cycle = 0;
                        irr_state = IRR_IDLE;

                        break;
                    }
                    irr_state = IRR_ZONE2_WATERING;
                    irr_state_start = HAL_GetTick();

                    StartZone2();
                }

            }

            break;

        }

        default:
        {
            Debug_Print("[ERROR] Invalid irrigation state -> reset\r\n");

            StopAllIrrigation();
            Relay_Release();

            zone1_cycle = 0;
            zone2_cycle = 0;

            irr_state = IRR_IDLE;

            break;
        }
    }
}

/* =================================== */

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
    uint32_t seq = 0;

    Debug_Print("[REQ] Parse frame: ");
    Debug_Print(frame);
    Debug_Print("\r\n");

    /* 1. PARSE + VALIDATE REQ */
    if (sscanf(frame, "<REQ,SEQ=%lu>", &seq) != 1)
    {
        Debug_Print("[ERROR] INVALID REQ\r\n");
        return;
    }

    Debug_Print("[REQ] Valid\r\n");


    /* 2. REQUEST SensorTask */
    Debug_Print("[REQ] Request SensorTask\r\n");

    osStatus status = osSignalSet(SensorTaskHandle, SENSOR_READ_SIGNAL);

    if (status < 0)
    {
        Debug_Print("[ERROR] SENSOR SIGNAL FAILED\r\n");
        return;
    }


    /* 3. WAIT SENSOR READY */
    osEvent event = osSignalWait(SENSOR_READY_SIGNAL, 2000);

    if (event.status != osEventSignal)
    {
        Debug_Print("[ERROR] SENSOR TASK TIMEOUT\r\n");
        return;
    }

    Debug_Print("[REQ] Sensor data ready\r\n");


    /* 4. SEND DATA */
    send_Data(seq);
}

static void Process_Command(char *cmd)
{
    uint32_t seq = 0;

    Debug_Print("[CMD] Parse frame: ");
    Debug_Print(cmd);
    Debug_Print("\r\n");

    /* 1. PARSE SEQ */
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

    /* 2. DUPLICATE PROTECTION
     * ESP32 retries the exact same frame with the same SEQ when ACK is lost.
     * In that case, do NOT execute the relay/mode command again; only resend ACK.
     * Lưu ý: kiểm tra cả SEQ lẫn nội dung command, chứ không chỉ kiểm tra SEQ.
     * Cách này an toàn hơn trong trư�?ng hợp ESP32 reboot và sequence bắt đầu lại. */
    if (last_cmd_valid && last_cmd_seq == seq && strcmp(last_cmd, cmd) == 0 && (HAL_GetTick() - last_cmd_tick) <= CMD_DUPLICATE_MS)
    {
        Debug_Print("[CMD] Duplicate -> ACK only\r\n");
        send_ACK(seq);
        return;
    }

    /* 3. COPY COMMAND */
    strncpy(control_command, cmd, sizeof(control_command) - 1);
    control_command[sizeof(control_command) - 1] = '\0';

    /* 4. WAKE ControlTask */
    Debug_Print("[CMD] Request ControlTask\r\n");

    osStatus status = osSignalSet(IrrigationTaskHandle, CONTROL_EXEC_SIGNAL);

    if (status < 0)
    {
        Debug_Print("[ERROR] CONTROL SIGNAL FAILED\r\n");
        return;
    }

    /* 5. WAIT CONTROL COMPLETE */
    osEvent event = osSignalWait(CONTROL_OK_SIGNAL | CONTROL_ERROR_SIGNAL, 2000);

    if (event.status != osEventSignal)
    {
        Debug_Print("[ERROR] CONTROL TASK TIMEOUT\r\n");
        return;
    }

    /* Command thực thi thành công */
    if (event.value.signals & CONTROL_OK_SIGNAL)
    {
        Debug_Print("[CMD] Control successful\r\n");

        /* Store BEFORE sending ACK. If ACK is lost, ESP32 will retry the same
         * CMD and STM32 can safely resend ACK without executing it twice. */
        last_cmd_seq = seq;
        last_cmd_tick = HAL_GetTick();
        last_cmd_valid = 1;
        strncpy(last_cmd, cmd, sizeof(last_cmd) - 1);
        last_cmd[sizeof(last_cmd) - 1] = '\0';

        send_ACK(seq);
    }

    /* Command không hợp lệ hoặc không thực thi được */
    else if (event.value.signals & CONTROL_ERROR_SIGNAL)
    {
        Debug_Print("[ERROR] CONTROL FAILED - NO ACK\r\n");
        return;
    }
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

        osDelay(1);	// Nhuong CPU cho task khác 1ms
    }

    return 1;
}

static void send_Data(uint32_t seq)
{
	/* Chuyển E32 từ Power-Saving Mode -> Normal Mode để TX DATA */
	Debug_Print("[LORA] Switch E32 -> NORMAL for Data\r\n");
	LoRa_SetNormalMode();

    if (!LoRa_WaitReady(100))
    {
        Debug_Print("[ERROR] E32 NORMAL NOT READY\r\n");
        return;
    }

    /* Cho ESP32 transmitter đủ time chuyển WAKE-UP -> NORMAL RX */
    osDelay(30);

    /* <DATA,seq,temp,hum,soil1,soil2,valve1,valve2,mode,battery_percent> */
    snprintf(tx_buff, sizeof(tx_buff), "<D,%lu,%d,%d,%.2f,%.2f,%d,%d,%d,%d>",
    									seq, temp, humi, sm1, sm2,
										(valve1_state == VALVE_ON) ? 1 : 0,
										(valve2_state == VALVE_ON) ? 1 : 0,
										(irr_mode == MODE_AUTO) ? 1 : 0,
    									battery_percent);

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

    snprintf(ack_buff, sizeof(ack_buff), "<ACK,SEQ=%lu>", seq);

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

static float Soil_ADC_ToPercent(uint16_t adc, uint16_t adc_dry, uint16_t adc_wet)
{
    float moisture;

    /* TH phổ biến: DRY ADC > WET ADC */
    moisture = ((float)adc_dry - (float)adc) * 100.0f / ((float)adc_dry - (float)adc_wet);

    /* �?ảm bảo giá trị độ ẩm nằm trong khoảng [0, 100]% */
    if (moisture > 100.0f)
        moisture = 100.0f;

    if (moisture < 0.0f)
        moisture = 0.0f;

    return moisture;
}

static void read_Sensors(void)
{
    /* 1. SOIL MOISTURE */

    // Read ADC Raw
    soil1_adc = read_ADC(ADC_CHANNEL_0);
    soil2_adc = read_ADC(ADC_CHANNEL_1);

    // Chuyển ADC -> % độ ẩm
    sm1 = Soil_ADC_ToPercent(soil1_adc, SOIL1_ADC_DRY, SOIL1_ADC_WET);
    sm2 = Soil_ADC_ToPercent(soil2_adc, SOIL2_ADC_DRY, SOIL2_ADC_WET);

    /* 2. DHT11 */
    if (DHT11_Read_Data(&DHT11))
    {
        temp = DHT11.Temperature;
        humi = DHT11.Humidity;
    }

    /* 3. BATTERY */
    read_Battery();

    /* Sensor data đã được cập nhật */
    last_sensor_update_tick = HAL_GetTick();
    sensor_data_valid = 1;
}

static void read_Battery(void)
{
    ADC_ChannelConfTypeDef sConfig = {0};

    /* Chuyển ADC sang channel đo Battery */
    sConfig.Channel = BATTERY_ADC_CHANNEL;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_71CYCLES_5;

    HAL_ADC_ConfigChannel(&hadc1, &sConfig);

    /* Read điện áp Battery */
    battery_voltage = Battery_ReadVoltage();

    /* Chuyển điện áp -> % */
    battery_percent = Battery_GetPercent(battery_voltage);
}

static uint8_t Can_Enter_Stop_Mode(void)
{
	#if DEBUG_NO_STOP
    	return 0;
	#endif

    /* Chỉ được STOP khi toàn bộ hệ thống đang IDLE */

    /* 1. AUTO irrigation đang chạy (AUTO chỉ được STOP khi đang IDLE) */
    if (irr_mode == MODE_AUTO)
    {
        if (irr_state != IRR_IDLE)
        {
            return 0;
        }
    }

    /* 2. Relay đang thuộc MANUAL hoặc AUTO */
    if (relay_owner != RELAY_OWNER_NONE)
    {
        return 0;
    }

    /* 3. Irrigation waiting SensorTask đo */
    if (irr_measure_pending)
    {
        return 0;
    }

    /* 4. UART đang nhận một frame hoặc đã nhận frame nhưng LoRaTask chưa xử lý */
    if (frame_receive || frame_ready)
    {
        return 0;
    }

    /* 5. E32 đang bận TX/RX */
    if(HAL_GPIO_ReadPin(LORA_AUX_PORT,LORA_AUX_PIN) == GPIO_PIN_RESET)
    {
        return 0;
    }

    return 1;
}

static void Enter_Stop_Mode(void)
{
    /* =========================================================
     * 1. ATOMIC FINAL CHECK
     * ========================================================= */

    __disable_irq();

    /* Nếu đã có frame or đang nhận frame or UART đã có byte wait xử lý -> No STOP */
    if (lora_wakeup_flag || rtc_wakeup_flag || frame_ready || frame_receive || __HAL_UART_GET_FLAG(&huart1, UART_FLAG_RXNE))
    {
        __enable_irq();
        return;
    }

    /* =========================================================
     * 2. CLEAR OLD WAKE FLAGS
     * ========================================================= */

    __HAL_GPIO_EXTI_CLEAR_IT(LORA_AUX_PIN);		// Clear EXTI pending cũ để tránh vừa vào STOP đã wake

    HAL_NVIC_ClearPendingIRQ(EXTI15_10_IRQn);	// Clear NVIC pending của EXTI15

    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WU);			// Clear Power Wakeup Flag nếu còn

    /* =========================================================
     * 3. PREPARE STOP
     * ========================================================= */

    // 1. Stop HAL TICK: HAL Timebase hiện tại = TIM2
    HAL_SuspendTick();

    // 2. STOP FreeRTOS SysTick
    SysTick->CTRL &= ~SysTick_CTRL_TICKINT_Msk;

    // Clear SysTick pending interrupt cũ
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;

    /* =========================================================
     * 4. ENTER STOP MODE (WFI = Wait For Interrupt)
     * ========================================================= */

    HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);

    /* =========================================================
     * 5. WAKE-UP
     * ========================================================= */

    SystemClock_Config();

    HAL_ResumeTick();

    SysTick->CTRL |= SysTick_CTRL_TICKINT_Msk;

    /* Khi enable IRQ: EXTI AUX đang pending sẽ chạy vào callback và set lora_wakeup_flag = 1 */
    __enable_irq();
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

        /* Như�?ng CPU cho các FreeRTOS task khác.
         * Tránh busy-wait chiếm CPU liên tục khi E32 đang BUSY */
        osDelay(1);
    }

    return 1;
}

static void LoRa_SetNormalMode(void)
{
    /* M1 = 0, M0 = 0 */
    HAL_GPIO_WritePin(LORA_M1_PORT, LORA_M1_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LORA_M0_PORT, LORA_M0_PIN, GPIO_PIN_RESET);

    osDelay(5);

}

static void LoRa_SetPowerSavingMode(void)
{
    /* M1 = 1, M0 = 0 */
    HAL_GPIO_WritePin(LORA_M1_PORT, LORA_M1_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(LORA_M0_PORT, LORA_M0_PIN, GPIO_PIN_RESET);

    osDelay(5);
}

static void Debug_Print(const char *msg)
{
    HAL_UART_Transmit(&huart2, (uint8_t *)msg, strlen(msg), 100);
}

static void Irrigation_Measure(void)
{
    Debug_Print("[AUTO] Request sensor measurement\r\n");

    irr_measure_pending = 1;

    osStatus status = osSignalSet(SensorTaskHandle, IRRIGATION_READ_SIGNAL);

    if (status < 0)
    {
        Debug_Print("[ERROR] Irrigation sensor signal failed\r\n");

        /* Rollback trạng thái pending nếu gửi signal thất bại */
        irr_measure_pending = 0;
    }
}

static void Relay_Manual(void)
{
	StopAllIrrigation();

    /* Khi Manual vào: dừng Auto ngay */
    irr_state = IRR_IDLE;
    relay_owner = RELAY_OWNER_MANUAL;

    Debug_Print("[RELAY] Owner = MANUAL\r\n");
}

static void Relay_Auto(void)
{
    if(relay_owner == RELAY_OWNER_MANUAL) return;

    relay_owner = RELAY_OWNER_AUTO;

    Debug_Print("[RELAY] Owner = AUTO\r\n");
}

static void Relay_Release(void)
{
    relay_owner = RELAY_OWNER_NONE;

    Debug_Print("[RELAY] Owner released\r\n");
}

void RTC_Print_Time(void)
{
    RTC_TimeTypeDef sTime;
    RTC_DateTypeDef sDate;

    HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);

    HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);

    char msg[100];

    snprintf(msg, sizeof(msg), "RTC %02d:%02d:%02d - %02d/%02d/20%d\r\n",
             sTime.Hours,
             sTime.Minutes,
             sTime.Seconds,
             sDate.Date,
             sDate.Month,
             sDate.Year);

    Debug_Print(msg);
}

void RTC_SetAlarmAfterSeconds(uint8_t seconds)
{
    RTC_TimeTypeDef sTime;
    RTC_AlarmTypeDef sAlarm = {0};

    /* Clear RTC Alarm cũ trước khi set alarm mới */
    HAL_RTC_DeactivateAlarm(&hrtc, RTC_ALARM_A);
    __HAL_RTC_ALARM_CLEAR_FLAG(&hrtc, RTC_FLAG_ALRAF);
    __HAL_RTC_ALARM_EXTI_CLEAR_FLAG();

    HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);

    uint8_t alarm_sec, alarm_min, alarm_hour;
    uint32_t total_sec;

    total_sec = sTime.Seconds + seconds;

    alarm_sec = total_sec % 60;

    total_sec = sTime.Minutes + (total_sec / 60);

    alarm_min = total_sec % 60;

    alarm_hour = (sTime.Hours + (total_sec / 60)) % 24;

    sAlarm.AlarmTime.Hours = alarm_hour;

    sAlarm.AlarmTime.Minutes = alarm_min;

    sAlarm.AlarmTime.Seconds = alarm_sec;

    sAlarm.Alarm = RTC_ALARM_A;

    /* Set alarm với interrupt vì sẽ có 1 hàm callback được gọi khi RTC alarm xảy ra */
    HAL_RTC_SetAlarm_IT(&hrtc, &sAlarm, RTC_FORMAT_BIN);

    Debug_Print("[RTC] Alarm Set!\r\n");
}

static uint8_t Process_Control_Command(void)
{

    uint8_t control_success = 0;

    /* MODE CONTROL:
     *   <CMD,SEQ=x,MODE=AUTO>
     *   <CMD,SEQ=x,MODE=MANUAL> */

    if (strstr(control_command, "MODE=AUTO") != NULL)
    {
        Debug_Print("[CONTROL] MODE -> AUTO\r\n");

        /* Safely stop any manual/previous irrigation before AUTO takes over. */
        StopAllIrrigation();
        Relay_Release();

        irr_mode = MODE_AUTO;
        irr_state = IRR_IDLE;
        irr_measure_pending = 0;

        sensor_data_valid = 0;			// Xóa value cũ để đo lại sensor ngay khi vào AUTO.
        last_sensor_update_tick = 0;	// Reset timer

        zone1_cycle = 0;
        zone2_cycle = 0;
        last_cmd_time = 0;	// Reset timer -> �?ể timer cũ không ảnh hưởng tới chế độ mới.

        control_success = 1;
    }
    else if (strstr(control_command, "MODE=MANUAL") != NULL)
    {
        Debug_Print("[CONTROL] MODE -> MANUAL\r\n");

        /* Cancel AUTO immediately and leave all outputs in a safe OFF state. */
        StopAllIrrigation();
        Relay_Release();

        irr_mode = MODE_MANUAL;
        irr_state = IRR_IDLE;
        irr_measure_pending = 0;
        zone1_cycle = 0;
        zone2_cycle = 0;
        last_cmd_time = 0;	// Reset timer

        control_success = 1;
    }

    /* ZONE 1 */
    else if (strstr(control_command, "ZONE=1") != NULL)
    {
    	if (irr_mode != MODE_MANUAL)
    	{
    	    Debug_Print("[CONTROL] ZONE1 rejected - not in MANUAL mode\r\n");
    	    control_success = 0;
    	}
    	else if (strstr(control_command, "IRR=ON") != NULL)
        {
            Debug_Print("[CONTROL] ZONE1 -> ON\r\n");

            /* Nếu Zone1 đã đang được MANUAL tưới
             * thì không restart relay/pump */
            if (relay_owner == RELAY_OWNER_MANUAL && valve1_state == VALVE_ON)
            {
                Debug_Print("[CONTROL] ZONE1 already ON\r\n");

                /* Start/refresh manual watering safety timer. */
                last_cmd_time = HAL_GetTick();

                HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET);

                control_success = 1;
            }
            else
            {
                /* Zone1 chưa chạy.
                 * Có thể Zone2 đang chạy -> Relay_Manual()
                 * -> Stop hệ thống cũ trước khi chuyển sang Zone1 */
                Relay_Manual();

                StartZone1();

                last_cmd_time = HAL_GetTick();

                HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET);

                control_success = 1;
            }

        }
        else if (strstr(control_command, "IRR=OFF") != NULL)
        {
            Debug_Print("[CONTROL] ZONE1 -> OFF\r\n");

            if (relay_owner == RELAY_OWNER_MANUAL)
            {
                if (valve1_state == VALVE_ON)
                {
                    StopCurrentZone(RELAY_OWNER_MANUAL);

                    Relay_Release();
                    last_cmd_time = 0;

                    HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET);

                    control_success = 1;
                }
                else
                {
                    /* Zone1 vốn đã OFF. Không được làm ảnh hưởng Zone2 nếu Zone2 đang tưới */
                    Debug_Print("[CONTROL] ZONE1 already OFF\r\n");

                    control_success = 1;
                }
            }
            else if (relay_owner == RELAY_OWNER_NONE)
            {
                /* Relay vốn đã OFF -> trả SUCCESS */
                Debug_Print("[CONTROL] ZONE1 already OFF\r\n");

                last_cmd_time = 0;
                HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET);

                control_success = 1;
            }
            else
            {
                /* Không được release owner của task khác -> RELAY_OWNER_AUTO */
                Debug_Print("[CONTROL] ZONE1 OFF rejected - relay not owned by MANUAL\r\n");

                control_success = 0;
            }
        }
    }

    /* ZONE 2 */
    else if (strstr(control_command, "ZONE=2") != NULL)
    {
    	if (irr_mode != MODE_MANUAL)
    	{
    	    Debug_Print("[CONTROL] ZONE2 rejected - not in MANUAL mode\r\n");
    	    control_success = 0;
    	}
    	else if (strstr(control_command, "IRR=ON") != NULL)
        {
            Debug_Print("[CONTROL] ZONE2 -> ON\r\n");

            if (relay_owner == RELAY_OWNER_MANUAL && valve2_state == VALVE_ON)
            {
                Debug_Print("[CONTROL] ZONE2 already ON\r\n");

                /* Refresh safety timeout */
                last_cmd_time = HAL_GetTick();

                HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET);

                control_success = 1;
            }
            else
            {
                Relay_Manual();

                StartZone2();

                last_cmd_time = HAL_GetTick();

                HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET);

                control_success = 1;
            }
        }
        else if (strstr(control_command, "IRR=OFF") != NULL)
        {
            Debug_Print("[CONTROL] ZONE2 -> OFF\r\n");

            if (relay_owner == RELAY_OWNER_MANUAL)
            {
                if (valve2_state == VALVE_ON)
                {
                    StopCurrentZone(RELAY_OWNER_MANUAL);

                    Relay_Release();
                    last_cmd_time = 0;

                    HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET);

                    control_success = 1;
                }
                else
                {
                    Debug_Print("[CONTROL] ZONE2 already OFF\r\n");

                    control_success = 1;
                }
            }
            else if (relay_owner == RELAY_OWNER_NONE)
            {
                Debug_Print("[CONTROL] ZONE2 already OFF\r\n");

                last_cmd_time = 0;
                HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET);

                control_success = 1;
            }
            else
            {
                Debug_Print("[CONTROL] ZONE2 OFF rejected - relay not owned by MANUAL\r\n");

                control_success = 0;
            }
        }
    }

    UBaseType_t watermark = uxTaskGetStackHighWaterMark(NULL);	// Check mức stack thấp nhất còn lại của task.
    snprintf(dbg, sizeof(dbg), "[IrrigationTask STACK] Min free = %lu words\r\n", (unsigned long)watermark);
    Debug_Print(dbg);

    return control_success;
}

static void Manual_Water_Safety_Check(void)
{
    /* Manual irrigation safety timeout.
     * If an OFF command is lost (LoRa/Wi-Fi/ESP32 failure),
     * the STM32 still stops the pump and closes the valves locally. */
    if (relay_owner == RELAY_OWNER_MANUAL && last_cmd_time != 0U && (HAL_GetTick() - last_cmd_time) >= MANUAL_WATER_TIMEOUT_MS)
    {
        Debug_Print("[SAFETY] Manual watering timeout -> force OFF\r\n");

        StopCurrentZone(RELAY_OWNER_MANUAL);
        Relay_Release();
        last_cmd_time = 0U;

        HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET);		// Debug
    }
}

/* USER CODE END 4 */

/* USER CODE BEGIN Header_StartLoRaTask */
/**
  * @brief  Function implementing the LoRaTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartLoRaTask */
void StartLoRaTask(void const * argument)
{
  /* USER CODE BEGIN 5 */

	Debug_Print("[RTOS] LoRaTask started\r\n");

	/* Infinite loop */
    for (;;)
    {
    	/* ============================================
    	 * Ưu tiên xử lý frame nếu UART đã nhận đủ frame.
    	 * Cho phép LoRa hoạt động cả khi AUTO, không cho STM32 vào STOP mode.
    	 * ============================================ */
    	if (frame_ready)
    	{
    	    Debug_Print("[UART1] Frame received: ");
    	    Debug_Print(rx_frame_buffer);
    	    Debug_Print("\r\n");

    	    Process_Frame(rx_frame_buffer);

    	    /* Chỉ release buffer sau khi xử lý xong */
    	    frame_ready = 0;

    	    osDelay(1);
    	    continue;
    	}

        /* 1. CHECK SYSTEM IDLE */

        if (!Can_Enter_Stop_Mode())
        {
        	osDelay(5);
            continue;
        }

    	/* 2. E32 → POWER-SAVING */

        Debug_Print("[POWER] System IDLE => E32 -> POWER SAVING\r\n");
	    LoRa_SetPowerSavingMode();

	    if (!LoRa_WaitReady(100))
	    {
	    	/* Nếu E32 chưa ready, không được STOP */
	        Debug_Print("[ERROR] E32 POWER SAVING NOT READY\r\n");
	        continue;
	    }

	    /* 3. AUTO LOW POWER: Khi AUTO IDLE -> ngủ bằng RTC */

	    /* AUX can toggle while E32 changes operating mode */
	    __disable_irq();

	    lora_wakeup_flag = 0;
	    rtc_wakeup_flag = 0;
	    wake_source = WAKE_NONE;
	    __HAL_GPIO_EXTI_CLEAR_IT(LORA_AUX_PIN);
	    HAL_NVIC_ClearPendingIRQ(EXTI15_10_IRQn);

	    __enable_irq();

	    if(irr_mode == MODE_AUTO && irr_state == IRR_IDLE)
	    {
	    	Debug_Print("[RTC] AUTO IDLE -> SET RTC\r\n");

	        RTC_SetAlarmAfterSeconds(RTC_WAKEUP_INTERVAL_SEC);
	    }

	    /* 4. STM32 → STOP MODE */

	    Debug_Print("[POWER] Enter STM32 STOP\r\n");
	    Enter_Stop_Mode();

	    /* 5. RTC WAKE or LORA AUX WAKE */
	    __disable_irq();

	    uint8_t woke_by_lora = lora_wakeup_flag;
	    uint8_t woke_by_rtc = rtc_wakeup_flag;
	    lora_wakeup_flag = 0;
	    rtc_wakeup_flag = 0;

	    __enable_irq();

	    if(woke_by_lora)			// Ưu tiên CMD từ user trong TH wake xảy ra đồng thời
	    {
	        wake_source = WAKE_LORA;
	    }
	    else if(woke_by_rtc)
	    {
	        wake_source = WAKE_RTC;
	    }
	    else
	    {
	        wake_source = WAKE_NONE;
	        continue;
	    }

	    if(wake_source == WAKE_RTC)
	    {
	        Debug_Print("[WAKE] Source = RTC\r\n");

	        if(irr_mode == MODE_AUTO && irr_state == IRR_IDLE)
	        {
	            Irrigation_Measure();
	        }

	        wake_source = WAKE_NONE;
	        continue;	// Sleep lại
	    }

	    if(wake_source == WAKE_LORA)
	    {
	        Debug_Print("[WAKE] Source = LoRa AUX\r\n");

	        /* WAIT LoRa UART FRAME (E32 TXD → STM32 RX USART1) */
	        if (!Wait_For_Frame(FRAME_TIMEOUT_MS))
	        {
	        	/* Wake nhưng không nhận đủ frame */
	            Debug_Print("[ERROR] UART1 FRAME TIMEOUT\r\n");

	            frame_ready = 0;			// Bỏ Frame cũ khi timeout
	            wake_source = WAKE_NONE;
	            continue;
	        }

		    Debug_Print("[UART1] Frame received: ");
		    Debug_Print(rx_frame_buffer);
		    Debug_Print("\r\n");

	        Process_Frame(rx_frame_buffer);
	        frame_ready = 0;

	        wake_source = WAKE_NONE;
	    }

        UBaseType_t watermark = uxTaskGetStackHighWaterMark(NULL);	// Check mức stack thấp nhất còn lại của task.
        snprintf(dbg, sizeof(dbg), "[LoRaTask STACK] Min free = %lu words\r\n", (unsigned long)watermark);
        Debug_Print(dbg);

	    /* Guard time trước khi chuyển mode E32 */
	    osDelay(10);
    }

  /* USER CODE END 5 */
}

/* USER CODE BEGIN Header_StartSensorTask */
/**
* @brief Function implementing the SensorTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartSensorTask */
void StartSensorTask(void const * argument)
{
  /* USER CODE BEGIN StartSensorTask */

	Debug_Print("[RTOS] SensorTask started\r\n");

	/* Infinite loop */
    for (;;)
    {
        /* Wait LoRaTask, IrrigationTask yêu cầu read sensor.
         * osWaitForever: SensorTask BLOCKED hoàn toàn khi không có yêu cầu */
        Debug_Print("[SENSOR TASK] WAITING\r\n");

        osEvent event = osSignalWait(SENSOR_READ_SIGNAL | IRRIGATION_READ_SIGNAL, osWaitForever);

        Debug_Print("[SENSOR TASK] WOKE UP\r\n");

        if (event.status == osEventSignal)
        {
        	uint32_t signals = event.value.signals;

            /* Nếu có ít nhất một yêu cầu read sensor, chỉ read toàn bộ sensor 1 lần */
        	if (signals & (SENSOR_READ_SIGNAL | IRRIGATION_READ_SIGNAL))
            {
            	Debug_Print("[SENSOR TASK] Read sensors\r\n");

                read_Sensors();

                Debug_Print("[SENSOR TASK] Sensor read complete\r\n");
            }

            /* Nếu LoRaTask yêu cầu dữ liệu */
            if (signals & SENSOR_READ_SIGNAL)
            {
                Debug_Print("[SENSOR TASK] LoRa data ready\r\n");

                /* Báo cho LoRaTask: dữ liệu sensor đã sẵn sàng */
                osSignalSet(LoRaTaskHandle, SENSOR_READY_SIGNAL);
            }

            /* Nếu AUTO irrigation yêu cầu dữ liệu */
            if (signals & IRRIGATION_READ_SIGNAL)
            {
            	Debug_Print("[SENSOR TASK] Irrigation data ready\r\n");

                irr_measure_pending = 0;

                /* Báo cho IrrigationTask: dữ liệu sensor đã sẵn sàng */
                osSignalSet(IrrigationTaskHandle, IRRIGATION_READY_SIGNAL);
            }

            /* Check mức stack thấp nhất còn lại của task */
            UBaseType_t watermark = uxTaskGetStackHighWaterMark(NULL);
            snprintf(dbg, sizeof(dbg), "[SensorTask STACK] Min free = %lu words\r\n", (unsigned long)watermark);
            Debug_Print(dbg);
        }
    }

  /* USER CODE END StartSensorTask */
}

/* USER CODE BEGIN Header_StartIrrigationTask */
/**
* @brief Function implementing the IrrigationTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartIrrigationTask */
void StartIrrigationTask(void const * argument)
{
  /* USER CODE BEGIN StartIrrigationTask */

    Debug_Print("[RTOS] IrrigationTask started\r\n");

    /* Infinite loop */
    for(;;)
    {
        /* 1. Kiểm tra CMD từ LoRaTask */
        osEvent event = osSignalWait(CONTROL_EXEC_SIGNAL,0);

        if(event.status == osEventSignal)
        {

            uint8_t result;

            result = Process_Control_Command();

            if(result)
            {
                osSignalSet(LoRaTaskHandle, CONTROL_OK_SIGNAL);
            }
            else
            {
                osSignalSet(LoRaTaskHandle, CONTROL_ERROR_SIGNAL);
            }

        }

        /* WAIT Sensor measurement COMPLETE */
        osEvent sensor_event = osSignalWait(IRRIGATION_READY_SIGNAL, 0);

        if(sensor_event.status == osEventSignal)
        {
            Debug_Print("[AUTO] Sensor measurement completed\r\n");
        }

        /* 2. Safety Manual timeout */
        Manual_Water_Safety_Check();

        /* 3. AUTO FSM */
        Irrigation_AutoUpdate();

        osDelay(100);
    }
  /* USER CODE END StartIrrigationTask */
}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM2 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM2) {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

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
