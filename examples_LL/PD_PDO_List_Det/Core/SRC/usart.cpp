/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    usart.cpp
  * @brief   This file provides code for the configuration
  *          of all used USART.
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
#include "usart.hpp"
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */

/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN Define */
#define DEBUG_USART                             USART1
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
 * @brief USART1 Init
 *        PB6 -> USART1_TX (AF0)
 *        PB7 -> USART1_RX (AF0)
 *        115200 8N1, TX+RX
 **************************************/
void Studio_USART1_Init(void)
{
  /* USER CODE BEGIN Studio_USART1_Init 0 */

  /* USER CODE END Studio_USART1_Init 0 */

  LL_USART_InitTypeDef USART_InitStruct = {0};
  LL_GPIO_InitTypeDef GPIO_InitStruct = {0};

  LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOB);

  /**USART1 GPIO Configuration
   *PB6     ------> USART1_TX
   *PB7     ------> USART1_RX
   */
  GPIO_InitStruct.Pin = LL_GPIO_PIN_6 | LL_GPIO_PIN_7;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_UP;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Alternate = LL_GPIO_AF0_USART1;
  LL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  LL_APB1_GRP2_EnableClock(LL_APB1_GRP2_PERIPH_USART1);

  USART_InitStruct.BaudRate = 115200;
  USART_InitStruct.DataWidth = LL_USART_DATAWIDTH_8B;
  USART_InitStruct.Parity = LL_USART_PARITY_NONE;
  USART_InitStruct.StopBits = LL_USART_STOPBITS_1;
  USART_InitStruct.TransferDirection = LL_USART_DIRECTION_TX_RX;
  USART_InitStruct.HardwareFlowControl = LL_USART_HWCONTROL_NONE;
  USART_InitStruct.OverSampling = LL_USART_OVERSAMPLING_16;
  LL_USART_Init(USART1, &USART_InitStruct);

  LL_USART_DisableAutoBaudRate(USART1);

  LL_USART_Enable(USART1);

  /* USER CODE BEGIN Studio_USART1_Init 1 */

  /* USER CODE END Studio_USART1_Init 1 */
}

/* USER CODE BEGIN ExternalFunctions */

/**
  * @brief  Send one byte over the debug USART (blocking).
  */
void BSP_USART1_WriteByte(uint8_t ch)
{
  while (LL_USART_IsActiveFlag_TXE(DEBUG_USART) == 0)
  {
  }
  LL_USART_TransmitData8(DEBUG_USART, ch);
}

/**
  * @brief  Wait forever for one byte (blocking).
  */
uint8_t BSP_USART1_ReadByte(void)
{
  while (LL_USART_IsActiveFlag_RXNE(DEBUG_USART) == 0)
  {
  }
  return LL_USART_ReceiveData8(DEBUG_USART);
}

/**
  * @brief  Wait up to timeout_ms for one byte.
  * @retval 0 on success, -1 on timeout
  */
int BSP_USART1_ReadByteTimeout(uint8_t *pch, uint32_t timeout_ms)
{
  uint32_t start = BSP_GetTick();
  while (LL_USART_IsActiveFlag_RXNE(DEBUG_USART) == 0)
  {
    if ((BSP_GetTick() - start) >= timeout_ms)
    {
      return -1;
    }
  }
  *pch = LL_USART_ReceiveData8(DEBUG_USART);
  return 0;
}

/* USER CODE END ExternalFunctions */

/* USER CODE BEGIN 0 */
/*
 * printf 重定向钩子必须 extern "C"：
 *   fputc / putchar / _write 是 C 运行库（microlib）按【C 符号名】回调的钩子，
 *   且 <cstdio> 已经把 ::fputc 作为 using 声明引入全局名字空间，
 *   在 C++ 里直接定义同名全局函数会报
 *       "declaration conflicts with target of using declaration already in scope"
 *   用 extern "C" 既能匹配运行库的调用约定，也避开这个冲突。
 */
extern "C" {

#if (defined (__CC_ARM)) || (defined (__ARMCC_VERSION) && (__ARMCC_VERSION >= 6010050))
/**
  * @brief  retarget printf() to the debug USART (Keil MDK / Arm Compiler)
  */
int fputc(int ch, FILE *f)
{
  BSP_USART1_WriteByte((uint8_t)ch);
  while (!LL_USART_IsActiveFlag_TC(DEBUG_USART));
  LL_USART_ClearFlag_TC(DEBUG_USART);
  return ch;
}
#elif defined(__ICCARM__)
int putchar(int ch)
{
  BSP_USART1_WriteByte((uint8_t)ch);
  while (!LL_USART_IsActiveFlag_TC(DEBUG_USART));
  LL_USART_ClearFlag_TC(DEBUG_USART);
  return ch;
}
#elif defined(__GNUC__)
int __io_putchar(int ch)
{
  BSP_USART1_WriteByte((uint8_t)ch);
  while (!LL_USART_IsActiveFlag_TC(DEBUG_USART));
  LL_USART_ClearFlag_TC(DEBUG_USART);
  return ch;
}

int _write(int file, char *ptr, int len)
{
  int DataIdx;
  for (DataIdx = 0; DataIdx < len; DataIdx++)
  {
    __io_putchar(*ptr++);
  }
  return len;
}
#endif

} /* extern "C" */
/* USER CODE END 0 */

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
