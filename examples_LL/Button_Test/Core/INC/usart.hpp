/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    usart.hpp
  * @brief   This file provides code for the configuration
  *          of all used USART.
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
#ifndef BUTTON_TEST_USART_HPP
#define BUTTON_TEST_USART_HPP


/* Includes ------------------------------------------------------------------*/
#include "main.hpp"
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* USER CODE BEGIN defines */
/* USER CODE END defines */

void Studio_USART1_Init(void);

/* USER CODE BEGIN Prototypes */
/* Blocking single-byte helpers usable from application code */
void     BSP_USART1_WriteByte(uint8_t ch);
uint8_t  BSP_USART1_ReadByte(void);
int      BSP_USART1_ReadByteTimeout(uint8_t *pch, uint32_t timeout_ms);
/* USER CODE END Prototypes */


#endif /* BUTTON_TEST_USART_HPP */
