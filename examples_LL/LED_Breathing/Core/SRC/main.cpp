/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    main.cpp
  * @brief   LED 呼吸灯 — TIM3_CH1 PWM 驱动 PA2 上的板载 LED（LL 库版本）
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

/* Includes ------------------------------------------------------------------*/
#include "main.hpp"
#include "rcc.hpp"
#include "gpio.hpp"
#include "tim.hpp"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* Sine lookup table: sin(k * pi/63) * 1000, k = 0..63
 * Generates a natural breathing curve (smooth acceleration at extremes).
 * Peak value 1000 must match the TIM3 ARR (LED_PWM_ARR in tim.h). */
#define BREATH_STEPS    (64u)
#define BREATH_DELAY_MS (15u)

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* 1 ms tick counter, incremented from SysTick_Handler() */
volatile uint32_t uwTick = 0;
/* USER CODE BEGIN PV */

static const uint16_t breath_table[BREATH_STEPS] = {
       0,   50,  100,  149,  198,  247,  295,  342,
     388,  434,  478,  521,  563,  604,  643,  680,
     716,  750,  782,  812,  840,  866,  890,  912,
     931,  948,  963,  975,  985,  992,  997, 1000,
    1000,  997,  992,  985,  975,  963,  948,  931,
     912,  890,  866,  840,  812,  782,  750,  716,
     680,  643,  604,  563,  521,  478,  434,  388,
     342,  295,  247,  198,  149,  100,   50,    0,
};

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* External functions --------------------------------------------------------*/
/* USER CODE BEGIN ExternalFunctions */

/* USER CODE END ExternalFunctions */

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */
  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  LL_APB1_GRP2_EnableClock(LL_APB1_GRP2_PERIPH_SYSCFG);
  LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_PWR);
  LL_PWR_EnableBkUpAccess();
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock (HSI 8 MHz) and the 1 ms SysTick time base */
  Studio_RCC_Init();
  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  Studio_GPIO_Init();
  Studio_TIM3_Init();
  /* USER CODE BEGIN 2 */

  /* Start TIM3_CH1 PWM on PA2 (LED_PWM)
     HAL: HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1); */
  BSP_LED_PWM_Start();

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* Breathe in: sweep sine table forward */
    for (uint8_t i = 0; i < BREATH_STEPS; i++)
    {
      /* HAL: __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, breath_table[i]); */
      BSP_LED_PWM_SetDuty(breath_table[i]);
      /* HAL: HAL_Delay(BREATH_DELAY_MS); */
      LL_mDelay(BREATH_DELAY_MS);
    }
    /* Breathe out: sweep sine table backward */
    for (int8_t i = (BREATH_STEPS - 1); i >= 0; i--)
    {
      BSP_LED_PWM_SetDuty(breath_table[i]);
      LL_mDelay(BREATH_DELAY_MS);
    }
    /* USER CODE END WHILE */
    /* USER CODE BEGIN 3 */

    /* USER CODE END 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief  Return the 1 ms tick counter maintained by SysTick_Handler().
  * @note   LL_mDelay() only polls the SysTick COUNTFLAG and keeps no counter,
  *         so this is the millisecond time base for application code
  *         (the LL replacement for HAL_GetTick()).
  * @retval Milliseconds since reset
  */
uint32_t BSP_GetTick() noexcept
{
  return uwTick;
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
[[noreturn]] void Error_Handler()
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
