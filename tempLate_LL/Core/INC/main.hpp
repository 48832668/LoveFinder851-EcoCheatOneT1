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
#ifndef __MAIN_HPP__
#define __MAIN_HPP__

#ifdef __cplusplus
extern "C" {
#endif

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
#include "py32f0xx_ll_i2c.h"
#include "py32f0xx_ll_usart.h"
#include "py32f0xx_ll_tim.h"
#include "py32f0xx_ll_crc.h"
#include "py32f0xx_ll_spi.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#ifdef __cplusplus
#include <cstdint>
#endif
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
void Error_Handler(void);
/* USER CODE BEGIN EFP */
/* 1 ms 计时基准，由 py32f003_it.c 的 SysTick_Handler() 自增。
   LL_mDelay() 只轮询 SysTick 的 COUNTFLAG、不维护计数器，也没有 LL_GetTick()，
   所以应用代码（以及从 HAL 移植过来、原本用 HAL_GetTick 的代码）用这个。
   uwTick 放在 __cplusplus 之外：C 编译的 py32f003_it.c 也要自增它。 */
extern volatile uint32_t uwTick;
#ifdef __cplusplus
std::uint32_t BSP_GetTick() noexcept;
#endif
/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
/* USER CODE BEGIN Private defines */
/* 系统时钟频率（HSI 24 MHz，与 Studio_RCC_Init() 保持一致）。
   注意：厂商 py32f0xx_ll_rcc.h 里已经定义了 HSI_VALUE（8 MHz，指振荡器基准），
   这里不要再定义 HSI_VALUE，否则 -Wmacro-redefined 警告。 */
#define SYSCLK_FREQ  24000000U   /*!< 系统时钟频率 (Hz) */
/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_HPP__ */
