/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    gpio.cpp
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
#include "gpio.hpp"
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
 *        PA0  FUSB_INT   EXTI0 rising
 *        PA1  KEY_INT    EXTI1 rising
 *        PA4  LCD_DC     output PP (initial low)
 *        PA6  LCD_RESET  output PP (initial low)
 *        PA7  LCD_CS     output PP (initial low)
 *        PA12 LCD_EN     output PP (initial low)
 *        PB5  CLK_INT    EXTI5 rising
 **************************************/
void Studio_GPIO_Init(void)
{
  /* USER CODE BEGIN Studio_GPIO_Init 0 */

  /* USER CODE END Studio_GPIO_Init 0 */

  LL_EXTI_InitTypeDef EXTI_InitStruct = {0};
  LL_GPIO_InitTypeDef GPIO_InitStruct = {0};

  LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOA);
  LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOB);

  /* ---- PA0 FUSB_INT / PA1 KEY_INT : digital input, no pull ---- */
  GPIO_InitStruct.Pin = FUSB_INT_Pin | KEY_INT_Pin;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* ---- PA4 / PA6 / PA7 / PA12 : push-pull outputs, driven low ---- */
  LL_GPIO_ResetOutputPin(GPIOA, LCD_DC_Pin | LCD_RESET_Pin | LCD_CS_Pin | LCD_EN_Pin);
  GPIO_InitStruct.Pin = LCD_DC_Pin | LCD_RESET_Pin | LCD_CS_Pin | LCD_EN_Pin;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_OUTPUT;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* ---- PB5 CLK_INT : digital input, no pull ---- */
  GPIO_InitStruct.Pin = CLK_INT_Pin;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  LL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* ---- EXTI line 0 (PA0) : always mapped to port A on PY32F003 ---- */
  EXTI_InitStruct.Line = LL_EXTI_LINE_0;
  EXTI_InitStruct.Mode = LL_EXTI_MODE_IT;
  EXTI_InitStruct.Trigger = LL_EXTI_TRIGGER_RISING;
  EXTI_InitStruct.LineCommand = ENABLE;
  LL_EXTI_Init(&EXTI_InitStruct);

  /* ---- EXTI line 1 (PA1) ---- */
  LL_EXTI_SetEXTISource(LL_EXTI_CONFIG_PORTA, LL_EXTI_CONFIG_LINE1);
  EXTI_InitStruct.Line = LL_EXTI_LINE_1;
  EXTI_InitStruct.Mode = LL_EXTI_MODE_IT;
  EXTI_InitStruct.Trigger = LL_EXTI_TRIGGER_RISING;
  EXTI_InitStruct.LineCommand = ENABLE;
  LL_EXTI_Init(&EXTI_InitStruct);

  /* ---- EXTI line 5 (PB5) ---- */
  LL_EXTI_SetEXTISource(LL_EXTI_CONFIG_PORTB, LL_EXTI_CONFIG_LINE5);
  EXTI_InitStruct.Line = LL_EXTI_LINE_5;
  EXTI_InitStruct.Mode = LL_EXTI_MODE_IT;
  EXTI_InitStruct.Trigger = LL_EXTI_TRIGGER_RISING;
  EXTI_InitStruct.LineCommand = ENABLE;
  LL_EXTI_Init(&EXTI_InitStruct);

  NVIC_SetPriority(EXTI0_1_IRQn, 0);
  NVIC_EnableIRQ(EXTI0_1_IRQn);

  NVIC_SetPriority(EXTI2_3_IRQn, 0);
  NVIC_EnableIRQ(EXTI2_3_IRQn);

  NVIC_SetPriority(EXTI4_15_IRQn, 0);
  NVIC_EnableIRQ(EXTI4_15_IRQn);

  /* USER CODE BEGIN Studio_GPIO_Init 1 */

  /* USER CODE END Studio_GPIO_Init 1 */
}

/* USER CODE BEGIN ExternalFunctions */
void BSP_LCD_DC_Set(void)     { LL_GPIO_SetOutputPin(LCD_DC_Port, LCD_DC_Pin); }
void BSP_LCD_DC_Reset(void)   { LL_GPIO_ResetOutputPin(LCD_DC_Port, LCD_DC_Pin); }
void BSP_LCD_CS_Set(void)     { LL_GPIO_SetOutputPin(LCD_CS_Port, LCD_CS_Pin); }
void BSP_LCD_CS_Reset(void)   { LL_GPIO_ResetOutputPin(LCD_CS_Port, LCD_CS_Pin); }
void BSP_LCD_Reset_Set(void)  { LL_GPIO_SetOutputPin(LCD_RESET_Port, LCD_RESET_Pin); }
void BSP_LCD_Reset_Reset(void){ LL_GPIO_ResetOutputPin(LCD_RESET_Port, LCD_RESET_Pin); }
void BSP_LCD_Backlight(uint8_t on)
{
  if (on) { LL_GPIO_SetOutputPin(LCD_EN_Port, LCD_EN_Pin); }
  else    { LL_GPIO_ResetOutputPin(LCD_EN_Port, LCD_EN_Pin); }
}
/* USER CODE END ExternalFunctions */

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
