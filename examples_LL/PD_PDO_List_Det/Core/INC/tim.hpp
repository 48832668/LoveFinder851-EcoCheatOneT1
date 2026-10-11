/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    tim.hpp
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
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __TIM3_H__
#define __TIM3_H__


/* Includes ------------------------------------------------------------------*/
#include "main.hpp"
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

#define LED_PWM_Pin     LL_GPIO_PIN_2
#define LED_PWM_Port    GPIOA

/* USER CODE BEGIN defines */
#define LED_PWM_ARR     65535u
/* USER CODE END defines */

void Studio_TIM3_Init(void);

/* USER CODE BEGIN Prototypes */
void BSP_LED_PWM_Start(void);
void BSP_LED_PWM_SetDuty(uint16_t duty);
/* USER CODE END Prototypes */


#endif /* __TIM3_H__ */
