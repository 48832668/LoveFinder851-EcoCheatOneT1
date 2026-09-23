/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    tim.cpp
  * @brief   This file provides code for the configuration
  *          of all used TIM3.
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2026 Puya Semiconductor Co.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by Puya under BSD 3-Clause license,
  * the License ; You may not use this file except in compliance with the
  * License.You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2016 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "tim.hpp"
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */

/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN Define */

/* USER CODE END Define */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN Macro */

/* USER CODE END Macro */

/* Public variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Private */

/* USER CODE END Private */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* External variables --------------------------------------------------------*/
/* USER CODE BEGIN EV */

/* USER CODE END EV */

/***************************************
 * @brief TIM3 Init — CH1 PWM on PA2 (LED_PWM)
 *        PSC = 0, ARR = 65535 (与 HAL 基础工程一致，计数器不启动)
 *
 *        HAL 对应关系：
 *          HAL_TIM_Base_Init + HAL_TIM_ConfigClockSource + HAL_TIM_PWM_Init
 *            -> LL_TIM_SetPrescaler / SetCounterMode / SetAutoReload / ...
 *          HAL_TIMEx_MasterConfigSynchronization
 *            -> LL_TIM_SetTriggerOutput + LL_TIM_DisableMasterSlaveMode
 *          HAL_TIM_PWM_ConfigChannel -> LL_TIM_OC_SetMode / SetPolarity / ...
 *          HAL_TIM_PWM_Start        -> BSP_LED_PWM_Start()
 **************************************/
void Studio_TIM3_Init(void)
{
  /* USER CODE BEGIN Studio_TIM3_Init 0 */

  /* USER CODE END Studio_TIM3_Init 0 */

  LL_GPIO_InitTypeDef GPIO_InitStruct = {0};
  LL_TIM_InitTypeDef    TIM_InitStruct    = {0};
  LL_TIM_OC_InitTypeDef TIM_OC_InitStruct = {0};

  LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOA);

  /**TIM3 GPIO Configuration
   *PA2     ------> TIM3_CH1
   *
   * 注意：PA2 上 TIM3_CH1 用的是 AF13。LL 头里 LL_GPIO_AF1_TIM3 也存在
   * 且能编译通过，但选错 AF 会导致 PWM 完全没有输出。
   */
  GPIO_InitStruct.Pin = LED_PWM_Pin;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Alternate = LL_GPIO_AF13_TIM3;
  LL_GPIO_Init(LED_PWM_Port, &GPIO_InitStruct);

  LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM3);

  /* 时基：等价 HAL_TIM_Base_Init + HAL_TIM_ConfigClockSource。
     这里用整体初始化 LL_TIM_Init()，不要逐个 LL_TIM_SetXxx() 拼 —— 后者容易漏位
     （SPI1 就是这么漏掉 FRXTH 的，见 examples_LL/LCD_Test/README.md）。
     LL_TIM_Init() 内部还会产生一次更新事件，把 PSC/ARR 立即装入影子寄存器。 */
  TIM_InitStruct.Prescaler         = 0;
  TIM_InitStruct.CounterMode       = LL_TIM_COUNTERMODE_UP;
  TIM_InitStruct.Autoreload        = LED_PWM_ARR;
  TIM_InitStruct.ClockDivision     = LL_TIM_CLOCKDIVISION_DIV1;
  TIM_InitStruct.RepetitionCounter = 0;
  LL_TIM_Init(TIM3, &TIM_InitStruct);

  LL_TIM_DisableARRPreload(TIM3);                    /* = TIM_AUTORELOAD_PRELOAD_DISABLE */
  LL_TIM_SetTriggerOutput(TIM3, LL_TIM_TRGO_RESET);  /* = HAL_TIMEx_MasterConfigSynchronization */
  LL_TIM_DisableMasterSlaveMode(TIM3);

  /* 通道：等价 HAL_TIM_PWM_ConfigChannel */
  TIM_OC_InitStruct.OCMode       = LL_TIM_OCMODE_PWM1;
  TIM_OC_InitStruct.OCState      = LL_TIM_OCSTATE_DISABLE;  /* 先不输出，由 BSP_LED_PWM_Start() 打开 */
  TIM_OC_InitStruct.OCNState     = LL_TIM_OCSTATE_DISABLE;
  TIM_OC_InitStruct.CompareValue = 0;
  TIM_OC_InitStruct.OCPolarity   = LL_TIM_OCPOLARITY_HIGH;
  TIM_OC_InitStruct.OCNPolarity  = LL_TIM_OCPOLARITY_HIGH;
  TIM_OC_InitStruct.OCIdleState  = LL_TIM_OCIDLESTATE_LOW;
  TIM_OC_InitStruct.OCNIdleState = LL_TIM_OCIDLESTATE_LOW;
  LL_TIM_OC_Init(TIM3, LL_TIM_CHANNEL_CH1, &TIM_OC_InitStruct);
  LL_TIM_OC_DisableFast(TIM3, LL_TIM_CHANNEL_CH1);
  LL_TIM_CC_EnableChannel(TIM3, LL_TIM_CHANNEL_CH1);

  /* USER CODE BEGIN Studio_TIM3_Init 1 */

  /* USER CODE END Studio_TIM3_Init 1 */
}

/* USER CODE BEGIN ExternalFunctions */

/**
  * @brief  Start PWM output on TIM3_CH1 (= HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1))
  */
void BSP_LED_PWM_Start(void)
{
  LL_TIM_EnableCounter(TIM3);
}

/**
  * @brief  Set the PWM duty cycle (= __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, v))
  * @param  duty 0..LED_PWM_ARR
  */
void BSP_LED_PWM_SetDuty(uint16_t duty)
{
  LL_TIM_OC_SetCompareCH1(TIM3, duty);
}

/* USER CODE END ExternalFunctions */

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
