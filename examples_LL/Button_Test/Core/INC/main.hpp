/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    main.hpp
  * @brief   This file provides code for the configuration
  *          of all used MAIN.
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
#ifndef BUTTON_TEST_MAIN_HPP
#define BUTTON_TEST_MAIN_HPP


/* Includes ------------------------------------------------------------------*/
#include "py32f0xx_ll_system.h"
#include "py32f0xx_ll_bus.h"
#include "py32f0xx_ll_rcc.h"
#include "py32f0xx_ll_pwr.h"
#include "py32f0xx_ll_utils.h"
#include "py32f0xx_ll_dma.h"
#include "py32f0xx_ll_cortex.h"
#include "py32f0xx_ll_exti.h"
#include "py32f0xx_ll_gpio.h"
#include "py32f0xx_ll_usart.h"
/* Peripherals used by the example projects (LED_Breathing / LCD_Test /
   LCD_DMA_Test). Harmless when unused: the LL headers are macro/inline only. */
#include "py32f0xx_ll_flash.h"
#include "py32f0xx_ll_tim.h"
#include "py32f0xx_ll_spi.h"
#include "py32f0xx_ll_i2c.h"
#include "py32f0xx_ll_crc.h"
/* USER CODE BEGIN Includes */
#include <cstdint>
#include <cstdio>
/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
[[noreturn]] void Error_Handler();
/* USER CODE BEGIN EFP */
/* 1 ms tick counter driven from SysTick_Handler() in py32f003_it.c.
   LL_mDelay() only polls the SysTick COUNTFLAG, so this counter is what
   application code (and HAL -> LL ports using HAL_GetTick) should use. */
extern volatile std::uint32_t uwTick;
std::uint32_t BSP_GetTick() noexcept;
/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
/* USER CODE BEGIN Private defines */
#define HSI_VALUE    8000000U   /*!< Value of the Internal oscillator in Hz */
#define SYSCLK_FREQ  8000000U   /*!< System clock frequency in Hz          */
/* USER CODE END Private defines */


#endif /* BUTTON_TEST_MAIN_HPP */
