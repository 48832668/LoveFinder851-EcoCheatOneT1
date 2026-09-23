/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    gpio.hpp
  * @brief   This file provides code for the configuration
  *          of all used GPIO.
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2026 Puya Semiconductor Co.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by Mcu Studio under BSD 3-Clause license,
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
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef LED_BREATHING_GPIO_HPP
#define LED_BREATHING_GPIO_HPP


/* Includes ------------------------------------------------------------------*/
#include "main.hpp"
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* USER CODE BEGIN defines */
/* USER CODE END defines */

/* Board pin map — identical to the HAL LED_Breathing project */
#define FUSB_INT_Pin    LL_GPIO_PIN_0
#define FUSB_INT_Port   GPIOA
#define KEY_INT_Pin     LL_GPIO_PIN_1
#define KEY_INT_Port    GPIOA
#define CLK_INT_Pin     LL_GPIO_PIN_5
#define CLK_INT_Port    GPIOB

void Studio_GPIO_Init(void);

/* USER CODE BEGIN Prototypes */

/* USER CODE END Prototypes */


#endif /* LED_BREATHING_GPIO_HPP */
