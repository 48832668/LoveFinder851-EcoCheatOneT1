/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    spi.cpp
  * @brief   This file provides code for the configuration
  *          of all used SPI.
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
#include "spi.hpp"
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */

/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN Define */
/* 单次传输的等待超时（毫秒）。HAL 用的是 HAL_MAX_DELAY（无限等），
   这里故意设成有限值：一旦 SPI 因为任何原因卡住，程序不会死等，
   屏幕会停在当前状态、主循环继续跑，便于定位问题。
   若确认硬件稳定，可以把它改大或去掉超时判断。 */
#define SPI1_TIMEOUT_MS     100U
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
 * @brief SPI1 Init
 *        PA3 -> SPI1_MOSI (AF10)
 *        PA5 -> SPI1_SCK  (AF0)
 *        Master, 2 lines, MSB first, 8 bit, CPOL=Low, CPHA=1Edge,
 *        NSS software, baudrate prescaler 2 (fPCLK/2 = 4 MHz)
 *
 *        HAL 对应关系：HAL_SPI_Init -> LL_SPI_Init（整体初始化）
 *
 *        !!! 重要 !!!
 *        这里必须用完整的 LL_SPI_Init()，不要用一堆 LL_SPI_SetXxx() 逐个拼。
 *        LL_SPI_Init() 除 CR1/CR2 的常规位之外，还会设置 8 位模式专用的
 *        RX FIFO 阈值（FRXTH，见 py32f0xx_ll_spi.c）：
 *            if (DataWidth == LL_SPI_DATAWIDTH_8BIT)
 *              LL_SPI_SetRxFIFOThreshold(SPIx, LL_SPI_RX_FIFO_TH_QUARTER);
 *        漏掉这一步会导致接收侧 FIFO 行为与 HAL 不一致，连续传输时出错。
 *        这也正是 PyStudio 的 LL 模板所生成的标准写法。
 **************************************/
void Studio_SPI1_Init(void)
{
  /* USER CODE BEGIN Studio_SPI1_Init 0 */

  /* USER CODE END Studio_SPI1_Init 0 */

  LL_GPIO_InitTypeDef GPIO_InitStruct = {0};
  LL_SPI_InitTypeDef  SPI_InitStruct  = {0};

  LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOA);

  /**SPI1 GPIO Configuration
   *PA3     ------> SPI1_MOSI
   *PA5     ------> SPI1_SCK
   *
   * 注意：两个引脚的 AF 不同，PA3 是 AF10，PA5 是 AF0，不要写反。
   */
  GPIO_InitStruct.Pin = LL_GPIO_PIN_3;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Alternate = LL_GPIO_AF10_SPI1;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = LL_GPIO_PIN_5;
  GPIO_InitStruct.Alternate = LL_GPIO_AF0_SPI1;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  LL_APB1_GRP2_EnableClock(LL_APB1_GRP2_PERIPH_SPI1);
  LL_APB1_GRP2_ForceReset(LL_APB1_GRP2_PERIPH_SPI1);
  LL_APB1_GRP2_ReleaseReset(LL_APB1_GRP2_PERIPH_SPI1);

  /* LL_SPI_Init() 只在 SPI 处于关闭状态时才写入配置 */
  LL_SPI_Disable(SPI1);

  SPI_InitStruct.TransferDirection = LL_SPI_FULL_DUPLEX;
  SPI_InitStruct.Mode              = LL_SPI_MODE_MASTER;
  SPI_InitStruct.DataWidth         = LL_SPI_DATAWIDTH_8BIT;
  SPI_InitStruct.ClockPolarity     = LL_SPI_POLARITY_LOW;
  SPI_InitStruct.ClockPhase        = LL_SPI_PHASE_1EDGE;
  SPI_InitStruct.NSS               = LL_SPI_NSS_SOFT;
  SPI_InitStruct.BaudRate          = LL_SPI_BAUDRATEPRESCALER_DIV2;
  SPI_InitStruct.BitOrder          = LL_SPI_MSB_FIRST;
  SPI_InitStruct.SlaveSpeedMode    = LL_SPI_SLAVE_SPEED_FAST;
  LL_SPI_Init(SPI1, &SPI_InitStruct);

  LL_SPI_Enable(SPI1);

  /* USER CODE BEGIN Studio_SPI1_Init 1 */

  /* USER CODE END Studio_SPI1_Init 1 */
}

/* USER CODE BEGIN ExternalFunctions */

/**
  * @brief  Blocking send of len bytes.
  *         Equivalent to HAL_SPI_Transmit(&hspi1, buf, len, HAL_MAX_DELAY).
  *
  *         必须与 HAL 的实现保持一致，三点不能省：
  *           1) 传输前后 SPE 的关闭/打开（复位内部 FIFO 与 OVR 状态）
  *           2) 结束时等 BSY 清零（否则紧接着翻 CS 会截断最后一个字节）
  *           3) 清 OVR（全双工模式不读 DR，必须手动清）
  */
void BSP_SPI1_Write(const uint8_t *buf, uint16_t len)
{
  volatile uint32_t tmpreg;
  uint32_t start;

  if ((buf == NULL) || (len == 0U))
  {
    return;
  }

  /* 1) 与 HAL_SPI_Transmit 一致：先关后开，复位 SPI 内部状态 */
  LL_SPI_Disable(SPI1);
  LL_SPI_Enable(SPI1);

  /* 2) 逐字节发送 */
  while (len-- > 0U)
  {
    start = BSP_GetTick();
    while (LL_SPI_IsActiveFlag_TXE(SPI1) == 0U)
    {
      if ((BSP_GetTick() - start) > SPI1_TIMEOUT_MS)
      {
        goto spi1_write_done;
      }
    }
    LL_SPI_TransmitData8(SPI1, *buf++);
  }

  /* 3) 等最后一字节从 TX FIFO 取走 */
  start = BSP_GetTick();
  while (LL_SPI_IsActiveFlag_TXE(SPI1) == 0U)
  {
    if ((BSP_GetTick() - start) > SPI1_TIMEOUT_MS)
    {
      goto spi1_write_done;
    }
  }
  /* 4) 等最后一字节完全移出（等价 HAL 的 SPI_EndRxTxTransaction） */
  start = BSP_GetTick();
  while (LL_SPI_IsActiveFlag_BSY(SPI1) != 0U)
  {
    if ((BSP_GetTick() - start) > SPI1_TIMEOUT_MS)
    {
      goto spi1_write_done;
    }
  }

spi1_write_done:
  /* 5) 清 OVR：等价 HAL 的 __HAL_SPI_CLEAR_OVRFLAG（先读 DR 再读 SR） */
  tmpreg = LL_SPI_ReceiveData8(SPI1);
  tmpreg = SPI1->SR;
  (void)tmpreg;
}

/**
  * @brief  Blocking send of one byte
  */
void BSP_SPI1_WriteByte(uint8_t data)
{
  BSP_SPI1_Write(&data, 1);
}

/* USER CODE END ExternalFunctions */

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
