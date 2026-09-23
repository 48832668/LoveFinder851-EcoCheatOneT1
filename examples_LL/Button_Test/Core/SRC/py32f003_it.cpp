/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    py32f003_it.cpp
  * @brief   This file provides code for the configuration
  *          of all used NVIC.
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
#include "py32f003_it.hpp"
/* USER CODE BEGIN Includes */
#include "gpio.hpp"
/* 按键库：EXTI 中断需要把事件分发给按键对象 */
#include "BUTTON.hpp"
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

/* External functions --------------------------------------------------------*/
/* USER CODE BEGIN EV */

/* USER CODE END EV */

/*****************************************************************************/
/*           Cortex-M Processor Interruption and Exception Handlers          */
/*****************************************************************************/

/*
 * 为什么这里必须 extern "C"：
 *   启动文件 startup_py32f003xx.s 用 `IMPORT SysTick_Handler` 这类汇编符号
 *   按【名字】引用中断入口。C++ 默认会做名字修饰（name mangling），
 *   不加 extern "C" 会导出成 _Z15SysTick_Handlerv 之类，汇编端找不到
 *   → 链接报 undefined symbol，或者向量表里是空指针。
 *   这是硬件 ABI 层面的要求，与"是否兼容 C"无关。
 */
extern "C" {

/**
 * @brief This function handles Non maskable interrupt.
 */
void NMI_Handler(void)
{
  /* USER CODE BEGIN NMI_Handler 0 */

  /* USER CODE END NMI_Handler 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_NMI_Handler 0 */

    /* USER CODE END W1_NMI_Handler 0 */
  }
}
/**
 * @brief This function handles Hard fault interrupt.
 */
void HardFault_Handler(void)
{
  /* USER CODE BEGIN HardFault_Handler 0 */

  /* USER CODE END HardFault_Handler 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_HardFault_Handler 0 */

    /* USER CODE END W1_HardFault_Handler 0 */
  }
}
/**
 * @brief This function handles System service call via SWI instruction.
 */
void SVC_Handler(void)
{
  /* USER CODE BEGIN SVC_Handler 0 */

  /* USER CODE END SVC_Handler 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_SVC_Handler 0 */

    /* USER CODE END W1_SVC_Handler 0 */
  }
}
/**
 * @brief This function handles Pendable request for system service.
 */
void PendSV_Handler(void)
{
  /* USER CODE BEGIN PendSV_Handler 0 */

  /* USER CODE END PendSV_Handler 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_PendSV_Handler 0 */

    /* USER CODE END W1_PendSV_Handler 0 */
  }
}
/**
 * @brief This function handles System tick timer.
 */
void SysTick_Handler(void)
{
  /* USER CODE BEGIN SysTick_Handler 0 */

  /* USER CODE END SysTick_Handler 0 */
  uwTick++;
  /* USER CODE BEGIN SysTick_Handler 1 */

  /* USER CODE END SysTick_Handler 1 */
}

/******************************************************************************/
/* Puya Peripheral Interrupt Handlers */
/* Add here the Interrupt Handlers for the used peripherals. */
/* For the available peripheral interrupt handler names, */
/* please refer to the startup file. */
/******************************************************************************/

/**
 * @brief This function handles EXTI0_1_IRQn interrupt.
 * @param None
 * @retval None
 */
void EXTI0_1_IRQHandler(void)
{
  /* USER CODE BEGIN EXTI0_1_IRQn 0 */

  /* USER CODE END EXTI0_1_IRQn 0 */
  if (LL_EXTI_IsActiveFlag(LL_EXTI_LINE_0) != 0U)
  {
    LL_EXTI_ClearFlag(LL_EXTI_LINE_0);
    /* PA0 FUSB_INT */
  }
  if (LL_EXTI_IsActiveFlag(LL_EXTI_LINE_1) != 0U)
  {
    /* PA1 KEY_INT —— 按键按下（下降沿）。
       dispatchExti() 内部会清挂起标志，再把中断分发给所有 EXTI 线匹配的
       按键实例（见 LoveFinderLibForPY32_LL/BUTTON/BUTTON.hpp）。
       注意：这里【不能】再自己 LL_EXTI_ClearFlag()，那是库的职责。 */
    (void)LoveFinderLib::Button::dispatchExti(LL_EXTI_LINE_1);
  }
  /* USER CODE BEGIN EXTI0_1_IRQn 1 */

  /* USER CODE END EXTI0_1_IRQn 1 */
}

/**
 * @brief This function handles EXTI2_3_IRQn interrupt.
 * @param None
 * @retval None
 */
void EXTI2_3_IRQHandler(void)
{
  /* USER CODE BEGIN EXTI2_3_IRQn 0 */

  /* USER CODE END EXTI2_3_IRQn 0 */
  /* USER CODE BEGIN EXTI2_3_IRQn 1 */

  /* USER CODE END EXTI2_3_IRQn 1 */
}

/**
 * @brief This function handles EXTI4_15_IRQn interrupt.
 * @param None
 * @retval None
 */
void EXTI4_15_IRQHandler(void)
{
  /* USER CODE BEGIN EXTI4_15_IRQn 0 */

  /* USER CODE END EXTI4_15_IRQn 0 */
  if (LL_EXTI_IsActiveFlag(LL_EXTI_LINE_5) != 0U)
  {
    LL_EXTI_ClearFlag(LL_EXTI_LINE_5);
    /* PB5 CLK_INT */
  }
  /* USER CODE BEGIN EXTI4_15_IRQn 1 */

  /* USER CODE END EXTI4_15_IRQn 1 */
}

} /* extern "C" */

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
