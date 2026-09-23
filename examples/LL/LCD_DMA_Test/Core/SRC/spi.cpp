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

  /*==========================================================================
   * DMA 配置 —— SPI1_TX -> DMA1_Channel1
   *
   * 与 .pysprj 严格对应：
   *     SPI1.DMA.body.SPI1_TX.channel = DMA1_Channel1
   * PyStudio 把这一段（dma_ll 局部模板）生成在 Studio_SPI1_Init() 内部，
   * 这里保持同样的位置与形状 —— 从 PyStudio 重新导出不会覆盖错位。
   *
   * ⚠️ PY32F003 的 DMA 请求**不是硬件固定的**：SYSCFG->CFGR3 用三个 5 位
   *    字段把请求源路由到通道 1/2/3，复位值全是 0（=ADC）。所以除了配置
   *    通道，**还必须调用 LL_SYSCFG_SetDMARemap_CHx()**，否则该通道永远
   *    收不到 SPI1_TX 请求 —— TC 标志不置位，ST7735 库会超时并永久退化成
   *    阻塞 SPI，现象就是"CPU 和 DMA 一样快"。
   *
   * ⚠️ 重映射寄存器在 SYSCFG 外设内，**必须先开 SYSCFG 时钟**，否则写入无效。
   *    PyStudio 的 LL 侧模板目前只生成 SetDMARemap 调用、不生成 SYSCFG 时钟
   *    使能（HAL 侧在 hal_msp.c 里有 __HAL_RCC_SYSCFG_CLK_ENABLE()），
   *    所以那一行放在下面独立的 USER CODE 段里，重新导出不会被冲掉。
   *==========================================================================*/

  /* SPI1_TX Init */
  LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA1);

  /* ⚠️ 必须先开 SYSCFG 时钟，再写重映射寄存器！
     顺序反了的话 SetDMARemap 写进一个没上时钟的外设 —— 写入被丢弃，
     通道永远收不到 SPI1_TX 请求，现象就是 tx:0 fail:1（自检超时后永久退化）。

     PyStudio 的 LL 侧模板缺这一行（HAL 侧在 hal_msp.c 里有
     __HAL_RCC_SYSCFG_CLK_ENABLE()），所以放在 USER CODE 段内，
     重新导出时不会被冲掉。 */
  /* USER CODE BEGIN SPI1_DMA_SYSCFG_CLK */
  LL_APB1_GRP2_EnableClock(LL_APB1_GRP2_PERIPH_SYSCFG);
  /* USER CODE END SPI1_DMA_SYSCFG_CLK */

  LL_DMA_SetDataTransferDirection(DMA1, LL_DMA_CHANNEL_1, LL_DMA_DIRECTION_MEMORY_TO_PERIPH);
  LL_DMA_SetPeriphIncMode(DMA1, LL_DMA_CHANNEL_1, LL_DMA_PERIPH_NOINCREMENT);
  LL_DMA_SetMemoryIncMode(DMA1, LL_DMA_CHANNEL_1, LL_DMA_MEMORY_INCREMENT);
  LL_DMA_SetPeriphSize(DMA1, LL_DMA_CHANNEL_1, LL_DMA_PDATAALIGN_BYTE);
  LL_DMA_SetMemorySize(DMA1, LL_DMA_CHANNEL_1, LL_DMA_MDATAALIGN_BYTE);
  LL_DMA_SetMode(DMA1, LL_DMA_CHANNEL_1, LL_DMA_MODE_NORMAL);
  LL_DMA_SetChannelPriorityLevel(DMA1, LL_DMA_CHANNEL_1, LL_DMA_PRIORITY_HIGH);
  /* DMA 外设（目的）地址 —— SPI1_TX 数据寄存器。
     这行是 LL 手写配置最容易漏的：不用 LL_DMA_Init() 时，
     必须自己调 LL_DMA_SetPeriphAddress() 把 CPAR 指到 SPI1->DR。
     漏掉则 CPAR=0，DMA 会把数据写到地址 0 造成总线挂死，
     TC 永远不置位 —— 现象正是自检超时（tx:0 f:1）后永久退化为阻塞。 */
  LL_DMA_SetPeriphAddress(DMA1, LL_DMA_CHANNEL_1, (uint32_t)&SPI1->DR);
  /* DMA channel map. */
  LL_SYSCFG_SetDMARemap_CH1(LL_SYSCFG_DMA_MAP_SPI1_TX);

  LL_APB1_GRP2_EnableClock(LL_APB1_GRP2_PERIPH_SPI1);
  LL_APB1_GRP2_ForceReset(LL_APB1_GRP2_PERIPH_SPI1);
  LL_APB1_GRP2_ReleaseReset(LL_APB1_GRP2_PERIPH_SPI1);

  /* LL_SPI_Init() 只在 SPI 处于关闭状态时才写入配置 */
  LL_SPI_Disable(SPI1);

  /* 传输方向：主机仅发送（Half-Duplex Master, Tx only）
   *   .pysprj: SPI1.MODE.Mode.value = MASTER:1LINE   （MISO 不使能，只用 MOSI）
   *   对应 LL_SPI_HALF_DUPLEX_TX = SPI_CR1_BIDIMODE | SPI_CR1_BIDIOE
   *
   * 对 ST7735 这种只写不读的屏来说这是最合适的模式：
   *   - 只用 MOSI 一根线，与 .pysprj 的引脚配置一致
   *   - **接收通道关闭 → 不会累积 OVR**，省掉全双工下必须做的清 OVR 动作
   * 全双工虽然也能用（读 DR 清 OVR），但那是白占一个引脚还多一层隐患。 */
  SPI_InitStruct.TransferDirection = LL_SPI_HALF_DUPLEX_TX;
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
