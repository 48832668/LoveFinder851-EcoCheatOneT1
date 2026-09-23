/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    main.cpp
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

/* Includes ------------------------------------------------------------------*/
#include "main.hpp"
#include "rcc.hpp"
#include "gpio.hpp"
#include "i2c.hpp"
#include "spi.hpp"
#include "usart.hpp"
#include "tim.hpp"
#include "crc.hpp"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "st7735.hpp"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* 1 ms tick counter, incremented from SysTick_Handler() */
volatile uint32_t uwTick = 0;
/* USER CODE BEGIN PV */

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

  /* Initialize all configured peripherals
     （与 HAL 版 LCD_Test 的外设集完全一致：GPIO / I2C1 / SPI1 / USART1 / TIM3 / CRC） */
  Studio_GPIO_Init();
  Studio_I2C1_Init();
  Studio_SPI1_Init();
  Studio_USART1_Init();
  Studio_TIM3_Init();
  Studio_CRC_Init();
  /* USER CODE BEGIN 2 */
  /* ST7735 上电演示：三行文字（使用 LoveFinderLibForPY32_LL 的 ST7735 库）
     字体 Font_7x10 —— 像素数据在共享库里, 本工程只声明编译哪些字
     (见 LoveFinderLib/FontLib/font_config.hpp 的 FONT_7X10_CHARS, 共 25 个字符)，
     未声明的字符渲染时自动跳过。 */
  ST7735_Init();
  ST7735_FillScreen(ST7735::BLACK);
  ST7735_WriteString(40, 12, "exp1_LCD_Test", Font_7x10, ST7735::WHITE, ST7735::BLACK);
  ST7735_WriteString(40, 32, "EcoCheatOneT1", Font_7x10, ST7735::CYAN,  ST7735::BLACK);
  ST7735_WriteString(40, 52, "HelloWorld!",   Font_7x10, ST7735::RED,   ST7735::BLACK);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */
    /* USER CODE BEGIN 3 */
    /* HAL 版为 LL_mDelay(100); */
    LL_mDelay(100);
    /* USER CODE END 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief  Return the 1 ms tick counter maintained by SysTick_Handler().
  * @note   LL_mDelay() only polls the SysTick COUNTFLAG and keeps no counter,
  *         so this is the millisecond time base for application code.
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
