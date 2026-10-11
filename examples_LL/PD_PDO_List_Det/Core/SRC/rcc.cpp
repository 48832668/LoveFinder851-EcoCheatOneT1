/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    rcc.cpp
  * @brief   This file provides code for the configuration
  *          of all used RCC.
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
#include "rcc.hpp"
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
 * @brief RCC Init
 *        HSI 24 MHz -> SYSCLK, AHB /1, APB1 /1, Flash latency 0
 *
 * 为什么用 24 MHz 而不是 8 MHz：
 *   本工程是 SPI 带宽受限的（SPI = PCLK/2）。
 *   PCLK 8 MHz  -> SPI 4 MHz  -> 80x50 一帧 8000 字节要 16.0 ms（上限 62.5 FPS）
 *   PCLK 24 MHz -> SPI 12 MHz -> 同样一帧只要 5.3 ms（上限 187 FPS）
 *   提高主频是这里唯一真实有效的加速手段 —— DMA 并不能突破 SPI 线速。
 *
 * PY32F003 的 HSI 支持 4/8/16/22.12/24 MHz 校准值。
 * Flash 等待周期：PyStudio 的规则是 sysclk <= 28 MHz 用 LATENCY_0，
 * 所以 24 MHz 保持 0 等待周期。
 **************************************/
void Studio_RCC_Init(void)
{
  /* USER CODE BEGIN Studio_RCC_Init 0 */

  /* USER CODE END Studio_RCC_Init 0 */

  LL_FLASH_SetLatency(LL_FLASH_LATENCY_0);
  while (LL_FLASH_GetLatency() != LL_FLASH_LATENCY_0)
  {
  }

  LL_RCC_HSI_Enable();
  LL_RCC_HSI_SetCalibFreq(LL_RCC_HSICALIBRATION_24MHz);
  while (LL_RCC_HSI_IsReady() != 1)
  {
  }

  LL_RCC_SetAHBPrescaler(LL_RCC_SYSCLK_DIV_1);
  LL_RCC_SetAPB1Prescaler(LL_RCC_APB1_DIV_1);
  LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_HSISYS);
  while (LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_HSISYS)
  {
  }

  /* Configure SysTick to generate a 1 ms time base (drives LL_mDelay) */
  LL_Init1msTick(SYSCLK_FREQ);
  /* Update the SystemCoreClock global variable */
  LL_SetSystemCoreClock(SYSCLK_FREQ);

  /* 注意：PY32 的 LL_InitTick() 只置位 CLKSOURCE|ENABLE，**没有置位 TICKINT**，
     所以 SysTick 中断默认是关闭的，SysTick_Handler() 根本不会被调用，
     uwTick / BSP_GetTick() 会永远停在 0。这里显式打开中断。
     （ST 原版 LL_InitTick 是带 TICKINT 的，Puya 这份漏了。） */
  NVIC_SetPriority(SysTick_IRQn, (1UL << __NVIC_PRIO_BITS) - 1UL);
  SysTick->CTRL |= SysTick_CTRL_TICKINT_Msk;

  /* USER CODE BEGIN Studio_RCC_Init 1 */

  /* USER CODE END Studio_RCC_Init 1 */
}

/* USER CODE BEGIN ExternalFunctions */

/* USER CODE END ExternalFunctions */

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
