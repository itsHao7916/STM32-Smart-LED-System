/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
TIM_HandleTypeDef htim3;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM3_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* =========================================================
 * 第一部分：腳位定義
 * ========================================================= */

#define GREEN_PORT GPIOC               // 綠燈使用 GPIOC
#define GREEN_PIN GPIO_PIN_5           // 綠燈連接 PC5

#define RED_PORT GPIOC                 // 紅燈使用 GPIOC
#define RED_PIN GPIO_PIN_6             // 紅燈連接 PC6

#define BUTTON_PORT GPIOC              // 按鈕使用 GPIOC
#define BUTTON_PIN GPIO_PIN_9          // 按鈕連接 PC9

/* =========================================================
 * 第二部分：時間參數
 * ========================================================= */

#define GREEN_TIME 3000U               // 綠燈恆亮 3 秒
#define YELLOW_TIME 6000U              // 黃燈總共呼吸 6 秒
#define BREATH_PERIOD 2000U            // 每次呼吸 2 秒，6 秒完成 3 次
#define BREATH_STEP 10U                // 每 10 毫秒更新一次呼吸亮度
#define FLASH_TIME 250U                // 紅燈每 250 毫秒切換亮滅
#define DEBOUNCE_TIME 30U              // 按鈕防彈跳時間 30 毫秒

#define PWM_MAX 999U                   // PWM 最大比較值，對應 ARR=999

/* =========================================================
 * 第三部分：系統狀態
 * ========================================================= */

typedef enum                           // 建立系統狀態型態
{
    STATE_GREEN,                       // 狀態一：綠燈恆亮
    STATE_YELLOW,                      // 狀態二：黃燈呼吸
    STATE_RED                          // 狀態三：紅燈持續閃爍
} SystemState;                         // 狀態型態名稱

static SystemState currentState = STATE_GREEN; // 系統預設為綠燈

/* =========================================================
 * 第四部分：時間變數
 * ========================================================= */

static uint32_t stateStartTime = 0U;    // 記錄目前狀態開始時間
static uint32_t lastFlashTime = 0U;     // 記錄紅燈上次切換時間
static uint32_t lastBreathTime = 0U;    // 記錄黃燈上次更新時間

/* =========================================================
 * 第五部分：按鈕變數
 * ========================================================= */

static GPIO_PinState lastRawButton = GPIO_PIN_SET; // 上一次原始按鈕電位
static GPIO_PinState stableButton = GPIO_PIN_SET;  // 防彈跳後的穩定電位
static uint32_t buttonChangeTime = 0U;             // 按鈕電位改變時間

/* =========================================================
 * 第六部分：隨機數變數
 * ========================================================= */

static uint32_t randomSeed = 0x12345678U; // 偽隨機數種子

/* =========================================================
 * 第七部分：函式宣告
 * ========================================================= */

static void AllLED_Off(void);                    // 關閉全部 LED
static void ChangeState(SystemState nextState);  // 切換系統狀態
static uint8_t ButtonPressed(void);              // 偵測按鈕
static void PrivacySystem_Update(void);          // 更新系統狀態

/* =========================================================
 * 第八部分：關閉所有 LED
 * ========================================================= */

static void AllLED_Off(void)            // 關閉所有 LED 的函式
{
    HAL_GPIO_WritePin(GREEN_PORT, GREEN_PIN, GPIO_PIN_RESET); // 關閉綠燈

    HAL_GPIO_WritePin(RED_PORT, RED_PIN, GPIO_PIN_RESET); // 關閉紅燈

    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 0U); // 黃燈亮度設為 0
}

/* =========================================================
 * 第九部分：切換系統狀態
 * ========================================================= */

static void ChangeState(SystemState nextState) // 切換狀態函式
{
    currentState = nextState;                 // 更新目前狀態

    stateStartTime = HAL_GetTick();           // 記錄狀態開始時間

    lastFlashTime = stateStartTime;           // 重設紅燈計時

    lastBreathTime = stateStartTime;          // 重設黃燈計時

    AllLED_Off();                             // 切換狀態前先關閉全部 LED

    switch (currentState)                     // 判斷要進入哪個狀態
    {
        case STATE_GREEN:                     // 如果進入綠燈狀態

            HAL_GPIO_WritePin(GREEN_PORT, GREEN_PIN, GPIO_PIN_SET); // 開啟綠燈

            break;                            // 結束綠燈設定

        case STATE_YELLOW:                    // 如果進入黃燈狀態

            // 黃燈亮度由主控制函式逐步更新

            break;                            // 結束黃燈設定

        case STATE_RED:                       // 如果進入紅燈狀態

            HAL_GPIO_WritePin(RED_PORT, RED_PIN, GPIO_PIN_SET); // 先開啟紅燈

            break;                            // 結束紅燈設定

        default:                              // 如果出現未定義狀態

            break;                            // 不執行其他動作
    }
}

/* =========================================================
 * 第十部分：按鈕防彈跳偵測
 * ========================================================= */

static uint8_t ButtonPressed(void)               // 偵測新的按鈕按下事件
{
    GPIO_PinState rawButton;                     // 儲存按鈕原始電位

    uint32_t now = HAL_GetTick();                // 取得目前時間

    rawButton = HAL_GPIO_ReadPin(BUTTON_PORT, BUTTON_PIN); // 讀取 PC9

    if (rawButton != lastRawButton)              // 如果按鈕原始電位改變
    {
        lastRawButton = rawButton;               // 記錄新的原始電位

        buttonChangeTime = now;                 // 記錄按鈕電位改變時間
    }

    if ((now - buttonChangeTime) >= DEBOUNCE_TIME) // 電位穩定 30 毫秒
    {
        if (rawButton != stableButton)           // 如果穩定後的電位有改變
        {
            stableButton = rawButton;            // 更新穩定後的按鈕電位

            if (stableButton == GPIO_PIN_RESET)  // 如果按鈕被按下
            {
                return 1U;                      // 回傳 1，表示按下
            }
        }
    }

    return 0U;                                   // 沒有新的按下事件
}

/* =========================================================
 * 第十一部分：主要控制函式
 * ========================================================= */

static void PrivacySystem_Update(void)           // 系統主要控制函式
{
    uint32_t now = HAL_GetTick();                // 取得目前系統時間

    uint32_t elapsed = now - stateStartTime;     // 計算目前狀態經過的時間

    uint32_t breathPosition = 0U;                // 記錄目前呼吸週期的位置

    uint32_t brightness = 0U;                    // 記錄 PWM 亮度

    uint8_t pressed = ButtonPressed();           // 偵測按鈕是否被按下

    switch (currentState)                        // 根據目前狀態執行功能
    {
        /* =================================================
         * 狀態一：綠燈恆亮 3 秒
         * ================================================= */

        case STATE_GREEN:                        // 進入綠燈狀態

            if (elapsed >= GREEN_TIME)           // 如果綠燈已經持續 3 秒
            {
                ChangeState(STATE_YELLOW);       // 切換至黃燈呼吸狀態
            }

            break;                               // 結束綠燈狀態

        /* =================================================
         * 狀態二：黃燈 6 秒內呼吸 3 次
         * ================================================= */

        case STATE_YELLOW:                       // 進入黃燈呼吸狀態

            if (elapsed >= YELLOW_TIME)          // 如果黃燈已經持續 6 秒
            {
                __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 0U); // 關閉黃燈

                randomSeed ^= randomSeed << 13;  // 產生偽隨機數第一步

                randomSeed ^= randomSeed >> 17;  // 產生偽隨機數第二步

                randomSeed ^= randomSeed << 5;   // 產生偽隨機數第三步

                if ((randomSeed & 1U) == 0U)     // 如果隨機結果為 0
                {
                    ChangeState(STATE_GREEN);    // 返回綠燈重新循環
                }
                else                            // 如果隨機結果為 1
                {
                    ChangeState(STATE_RED);      // 進入紅燈警示狀態
                }
            }
            else if ((now - lastBreathTime) >= BREATH_STEP) // 每 10 毫秒更新一次
            {
                lastBreathTime = now;            // 記錄這次更新時間

                breathPosition = elapsed % BREATH_PERIOD; // 取得 2 秒內的位置

                if (breathPosition <= BREATH_PERIOD / 2U) // 每次呼吸前 1 秒
                {
                    brightness = (breathPosition * PWM_MAX) / (BREATH_PERIOD / 2U); // 逐漸變亮
                }
                else                            // 每次呼吸後 1 秒
                {
                    brightness = ((BREATH_PERIOD - breathPosition) * PWM_MAX) / (BREATH_PERIOD / 2U); // 逐漸變暗
                }

                __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, brightness); // 更新 PWM 亮度
            }

            break;                               // 結束黃燈狀態

        /* =================================================
         * 狀態三：紅燈持續閃爍
         * ================================================= */

        case STATE_RED:                          // 進入紅燈警示狀態

            if (pressed == 1U)                    // 判斷是否按下按鈕
            {
                ChangeState(STATE_GREEN);        // 按下後返回綠燈
            }
            else if ((now - lastFlashTime) >= FLASH_TIME) // 每隔 250 毫秒
            {
                lastFlashTime = now;             // 記錄切換時間

                HAL_GPIO_TogglePin(RED_PORT, RED_PIN); // 切換紅燈亮滅
            }

            // 沒按按鈕時，保持 STATE_RED 狀態並持續閃爍

            break;                               // 結束紅燈狀態

        /* =================================================
         * 異常狀態處理
         * ================================================= */

        default:                                 // 如果系統進入未知狀態

            ChangeState(STATE_GREEN);           // 返回綠燈重新開始

            break;                               // 結束狀態判斷
    }
}

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
  MX_TIM3_Init();
  /* USER CODE BEGIN 2 */

  // 啟動 TIM3 Channel 3 PWM
  if (HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3) != HAL_OK)
  {
      Error_Handler();                             // 啟動失敗時進入錯誤處理
  }

  // 將黃色 LED 初始亮度設定為 0
  __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 0U);

  // 讀取按鈕初始狀態
  lastRawButton = HAL_GPIO_ReadPin(BUTTON_PORT, BUTTON_PIN);

  // 初始化按鈕穩定狀態
  stableButton = lastRawButton;

  // 記錄按鈕初始化時間
  buttonChangeTime = HAL_GetTick();

  // 將系統時間混入隨機種子
  randomSeed ^= HAL_GetTick();

  // 避免隨機種子為 0
  if (randomSeed == 0U)
  {
      randomSeed = 0x12345678U;                    // 使用非零隨機種子
  }

  // 設定系統初始狀態為綠燈
  ChangeState(STATE_GREEN);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

	  PrivacySystem_Update();     // 持續更新 LED、按鈕與系統狀態

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

  /** Configure the main internal regulator output voltage
  */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV4;
  RCC_OscInitStruct.PLL.PLLN = 85;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
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
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 169;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 999;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim3, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */

  /* USER CODE END TIM3_Init 2 */
  HAL_TIM_MspPostInit(&htim3);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, Green_led_Pin|Red_led_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : Green_led_Pin Red_led_Pin */
  GPIO_InitStruct.Pin = Green_led_Pin|Red_led_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : Button_Pin */
  GPIO_InitStruct.Pin = Button_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(Button_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
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
#ifdef USE_FULL_ASSERT
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
