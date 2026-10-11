/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    gpio.c
  * @brief   This file provides code for the configuration
  *          of all used GPIO.
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
#include "gpio.h"
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
 * @brief GPIO Init
 **************************************/
void Studio_GPIO_Init(void)
{
  /* USER CODE BEGIN Studio_GPIO_Init 0 */

  /* USER CODE END Studio_GPIO_Init 0 */

  LL_EXTI_InitTypeDef EXTI_InitStruct = {0};
  LL_GPIO_InitTypeDef GPIO_InitStruct = {0};

  LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOA);
  LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOB);

  GPIO_InitStruct.Pin = FUSB_INT_Pin;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = KEY_INT_Pin;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  LL_GPIO_ResetOutputPin(GPIOA, LCD_DC_Pin|LCD_RESET_Pin|LCD_CS_Pin|LCD_EN_Pin);
  GPIO_InitStruct.Pin = LCD_DC_Pin|LCD_RESET_Pin|LCD_CS_Pin|LCD_EN_Pin;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_OUTPUT;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = CLK_INT_Pin;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  LL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* ★ PA0 FUSB_INT：FUSB302 的 INT_N 是【开漏 + 低有效】，
   *   所以"有中断要处理"这个物理事件就是【由高变低】= 下降沿。
   *   ⚠️ 不要配成 RISING_FALLING 或 RISING —— 那样只会在"中断被读掉、
   *      引脚抬回高"的那一下进 ISR，真正要处理的那一刻反而毫无反应。 */
  EXTI_InitStruct.Line = LL_EXTI_LINE_0;
  EXTI_InitStruct.Mode = LL_EXTI_MODE_IT;
  EXTI_InitStruct.Trigger = LL_EXTI_TRIGGER_FALLING;
  EXTI_InitStruct.LineCommand = ENABLE;
  LL_EXTI_Init(&EXTI_InitStruct);

  /* ★ PA1 KEY_INT：按键是【外部上拉 + 按下接地】，低有效。
   *   本工程要求**两个沿都进中断**（RISING_FALLING）：
   *     下降沿 = 按下，上升沿 = 松开。
   *   ISR 只记录"发生了一个边沿 + 边沿后的引脚电平"，去抖和短按/长按
   *   判定全部放主循环做 —— 见 Core/SRC/app.cpp 的 KeyService()。 */
  EXTI_InitStruct.Line = LL_EXTI_LINE_1;
  EXTI_InitStruct.Mode = LL_EXTI_MODE_IT;
  EXTI_InitStruct.Trigger = LL_EXTI_TRIGGER_RISING_FALLING;
  EXTI_InitStruct.LineCommand = ENABLE;
  LL_EXTI_Init(&EXTI_InitStruct);
  LL_EXTI_SetEXTISource(LL_EXTI_CONFIG_PORTA, LL_EXTI_CONFIG_LINE1);

  EXTI_InitStruct.Line = LL_EXTI_LINE_5;
  EXTI_InitStruct.Mode = LL_EXTI_MODE_IT;
  EXTI_InitStruct.Trigger = LL_EXTI_TRIGGER_FALLING;
  EXTI_InitStruct.LineCommand = ENABLE;
  LL_EXTI_Init(&EXTI_InitStruct);
  LL_EXTI_SetEXTISource(LL_EXTI_CONFIG_PORTB, LL_EXTI_CONFIG_LINE5);

  NVIC_SetPriority(EXTI0_1_IRQn, 0);
  NVIC_EnableIRQ(EXTI0_1_IRQn);

  NVIC_SetPriority(EXTI2_3_IRQn, 0);
  NVIC_EnableIRQ(EXTI2_3_IRQn);

  NVIC_SetPriority(EXTI4_15_IRQn, 0);
  NVIC_EnableIRQ(EXTI4_15_IRQn);

  /* USER CODE BEGIN Studio_GPIO_Init 1 */

  /* ★★ 2026-10-09 手工补：PA0 / PA1 **必须**开内部上拉 ★★
   *
   * 【为什么放在 USER CODE 块里，而不是改上面生成的那两行】
   *   PY32Studio 的 .pysprj 里**根本没有** GPIO 上下拉这个配置项
   *   （已核对：整个 PD_PDO_FIX_Req.pysprj 搜不到 "pull" 字样），
   *   所以它生成出来永远是 LL_GPIO_PULL_NO。
   *   放在 USER CODE 块里，重新生成代码时这段不会被冲掉。
   *
   * 【为什么必须上拉】
   *   · PA0 FUSB_INT ← FUSB302 的 INT_N。数据手册 p.5 原文：
   *       "INT_N Open-Drain Output —— Active LOW open drain interrupt output
   *        used to prompt the processor to read the I2C register bits."
   *     p.33 给的外部上拉取值 RPU_INT = 1.0 / 4.7 kΩ。
   *     它**只会拉低、从不主动输出高** —— 没有上拉时"高"这个状态就是悬空，
   *     读回来的电平随机，EXTI 也会被噪声乱触发
   *     （表现就是"什么都没插，中断却一直在进"）。
   *   · PA1 KEY_INT ← 按键，外部 3V3 上拉 + 按下接地。
   *     该网络曾经因为外部上拉没贴而悬空抖动，导致"等按键"时好时坏
   *     （见 PD_PDO_List_Det 的 WaitForButton() 注释）。
   *     内部上拉与外部上拉并联，无害。
   *
   * ⚠️ 这段跑在 LL_EXTI_Init() 之后也没关系：PUPDR 与 EXTI 的触发沿
   *    是两个独立寄存器，互不影响。 */
  GPIO_InitStruct.Pin  = FUSB_INT_Pin | KEY_INT_Pin;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_UP;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE END Studio_GPIO_Init 1 */
}

/* USER CODE BEGIN ExternalFunctions */

/* USER CODE END ExternalFunctions */

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
