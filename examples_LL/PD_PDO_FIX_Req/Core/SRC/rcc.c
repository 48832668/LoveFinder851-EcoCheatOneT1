/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    rcc.c
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
#include "rcc.h"
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
 **************************************/
void Studio_RCC_Init(void)
{
  /* USER CODE BEGIN Studio_RCC_Init 0 */

  /* USER CODE END Studio_RCC_Init 0 */
  LL_FLASH_SetLatency(LL_FLASH_LATENCY_0);
  while(LL_FLASH_GetLatency() != LL_FLASH_LATENCY_0)
  {
  }

  LL_RCC_HSI_Enable();
  LL_RCC_HSI_SetCalibFreq(LL_RCC_HSICALIBRATION_24MHz);
  while(LL_RCC_HSI_IsReady() != 1)
  {
  }

  LL_RCC_SetAHBPrescaler(LL_RCC_SYSCLK_DIV_1);
  LL_RCC_SetAPB1Prescaler(LL_RCC_APB1_DIV_1);
  LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_HSISYS);
  while(LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_HSISYS)
  {
  }
  LL_Init1msTick(24000000);
  /* Update the SystemCoreClock global variable(which can be updated also through SystemCoreClockUpdate function) */
  LL_SetSystemCoreClock(24000000);

  /* USER CODE BEGIN Studio_RCC_Init 1 */

  /* ★★ 2026-10-09 手工补：打开 SysTick 中断 ★★
   *
   * 【为什么必须补】
   *   PY32 的 LL_Init1msTick() / LL_InitTick() **只置位 CLKSOURCE|ENABLE，
   *   没有置位 TICKINT**（ST 原版是带的，Puya 这份漏了）。
   *   不补这一句的后果：SysTick_Handler() 根本不会被调用 →
   *     · uwTick 恒为 0 → BSP_GetTick() 恒返回 0；
   *     · 库的 I2C 超时 (BSP_GetTick()-t0) >= 20 永远不成立
   *       → 总线卡住时**死循环**；
   *     · 本工程所有时间判据（收到 caps 后 24ms 内回 Request、按键去抖、
   *       各种状态机超时）全部失效。
   *   而这一切在编译期是 0 error / 0 warning 的 —— 完全静默。
   *   （PD_PDO_List_Det 的 rcc.cpp 里也有同一处修复。） */
  NVIC_SetPriority(SysTick_IRQn, (1UL << __NVIC_PRIO_BITS) - 1UL);
  SysTick->CTRL |= SysTick_CTRL_TICKINT_Msk;

  /* USER CODE END Studio_RCC_Init 1 */
}

/* USER CODE BEGIN ExternalFunctions */

/* USER CODE END ExternalFunctions */

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
