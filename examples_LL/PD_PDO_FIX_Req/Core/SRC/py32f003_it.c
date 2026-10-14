/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    py32f003_it.c
  * @brief   Interrupt Service Routines.
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
#include "py32f003_it.h"
/* USER CODE BEGIN Includes */
#include "irq_flags.h"   /* g_pdIrq / g_keyIrq / g_keyLevel / uwTick */
#include "gpio.h"        /* KEY_INT_Port / KEY_INT_Pin —— 读按键引脚电平 */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */

/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN Define */

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

/*****************************************************************************/
/*           Cortex-M Processor Interruption and Exception Handlers          */
/*****************************************************************************/

/**
 * @brief This function handles Non maskable interrupt.
 */
void NMI_Handler(void)
{
  /* USER CODE BEGIN NMI_Handler 0 */

  /* USER CODE END NMI_Handler 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_NMI_Handler 0 */

    /* USER CODE END W1_NMI_Handler 0 */
  }
}
/**
 * @brief This function handles Hard fault interrupt.
 */
void HardFault_Handler(void)
{
  /* USER CODE BEGIN HardFault_Handler 0 */

  /* USER CODE END HardFault_Handler 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_HardFault_Handler 0 */

    /* USER CODE END W1_HardFault_Handler 0 */
  }
}
/**
 * @brief This function handles System service call via SWI instruction.
 */
void SVC_Handler(void)
{
  /* USER CODE BEGIN SVC_Handler 0 */

  /* USER CODE END SVC_Handler 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_SVC_Handler 0 */

    /* USER CODE END W1_SVC_Handler 0 */
  }
}
/**
 * @brief This function handles Pendable request for system service.
 */
void PendSV_Handler(void)
{
  /* USER CODE BEGIN PendSV_Handler 0 */

  /* USER CODE END PendSV_Handler 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_PendSV_Handler 0 */

    /* USER CODE END W1_PendSV_Handler 0 */
  }
}
/**
 * @brief This function handles System tick timer.
 */
void SysTick_Handler(void)
{
  /* USER CODE BEGIN SysTick_Handler 0 */

  /* USER CODE END SysTick_Handler 0 */
  /* USER CODE BEGIN SysTick_Handler 1 */

  /* ★ 1ms 时基。
   *
   * ⚠️ 这一句是**整个工程所有超时/计时**的根：
   *    库的 I2C 超时（FUSB302_I2C_TIMEOUT_MS=20ms）、DMA 超时、
   *    "收到 caps 后必须在 24ms 内回 Request"、按键去抖 —— 全靠 uwTick。
   *
   * ⚠️⚠️ 但 SysTick 中断**默认是关的**：PY32 的 LL_Init1msTick() 只置位
   *    CLKSOURCE|ENABLE，漏了 TICKINT（ST 原版带，Puya 这份没有）。
   *    Studio_RCC_Init() 里手工补了 TICKINT，见 rcc.c 的 USER CODE 段。
   *    少了那一句，本函数永远不会被调用，uwTick 恒为 0，
   *    表现就是"I2C 超时永远不触发 + 所有状态机时间都不动"。 */
  uwTick++;

  /* USER CODE END SysTick_Handler 1 */
}

/******************************************************************************/
/* Puya Peripheral Interrupt Handlers */
/* Add here the Interrupt Handlers for the used peripherals. */
/* For the available peripheral interrupt handler names, */
/* please refer to the startup file. */
/******************************************************************************/

/**
 * @brief This function handles EXTI0_1_IRQn interrupt.
 * @param None
 * @retval None
 */
void EXTI0_1_IRQHandler(void)
{
  /* USER CODE BEGIN EXTI0_1_IRQn 0 */

  /* ==========================================================================
   * ★★ 本 ISR 只做一件事：置标志。**绝对不做 I2C / SPI** ★★
   *
   * 三条理由，缺一不可（PD_PDO_List_Det 上踩过，别删这段注释）：
   *
   *  1) 会掐死时基 —— 而且是**死锁**，不是"慢一点"。
   *     库的每次 I2C 访问都带 FUSB302_I2C_TIMEOUT_MS=20ms 的超时轮询，
   *     而那个超时的判据是 (BSP_GetTick() - t0) >= 20 —— 也就是**靠 uwTick**。
   *     uwTick 只在 SysTick_Handler() 里 ++，而 SysTick 的优先级是
   *     (1<<__NVIC_PRIO_BITS)-1 = 3（最低），本 ISR 是 0（最高）。
   *     → 在优先级 0 里等 tick，SysTick 永远进不来 → uwTick 冻住 →
   *       ">= 20ms" 这个条件**永远不成立** → 总线一旦卡住就是死循环。
   *     而"总线卡住"（芯片闩锁 / NACK）恰恰是 Hard Reset 之后真实发生过的场景。
   *
   *  2) 会撕裂总线。I2C1 是主循环和这里共用的。主循环可能正卡在一次
   *     多字节传输中间（readBytes() 的 N>=3 收尾时序有 6 次标志轮询），
   *     ISR 插进来重入 I2C 外设会直接写坏时序。
   *
   *  3) 没必要。中断的全部价值是"立刻知道有事"，而真正的处理
   *     （读 FIFO、组 RDO、发 Request）本来就必须在主循环里按状态机做。
   *     主循环是无 sleep 的忙循环，ISR → 服务 的实际延迟只有 ~100us，
   *     相对 tSenderResponse 的 24ms 完全够用。
   * ========================================================================== */

  /* ---- ① PA0 FUSB_INT：FUSB302 的 INT_N（开漏低有效，下降沿） ---- */
  if (LL_EXTI_IsActiveFlag(LL_EXTI_LINE_0) != 0U)
  {
    LL_EXTI_ClearFlag(LL_EXTI_LINE_0);

    /* 只置标志。主循环取走后去读 Interrupt(0x42)/Interrupta(0x3E) 等
     * R/C 寄存器 —— 那些寄存器是**闩锁**，晚读不丢信息。 */
    g_pdIrq = 1u;
    g_pdIrqCnt++;
  }

  /* ---- ② PA1 KEY_INT：按键（外部上拉 + 按下接地，**两个沿都触发**） ---- */
  if (LL_EXTI_IsActiveFlag(LL_EXTI_LINE_1) != 0U)
  {
    LL_EXTI_ClearFlag(LL_EXTI_LINE_1);

    /* 只置标志 + 记下此刻的电平。
     *
     * ⚠️ 这里**不做去抖**。机械按键一次按下会产生好几个边沿
     *    （典型 1~20ms 内抖动 2~10 次），如果在这里"每来一个边沿就算按下一次"，
     *    一次物理按键会被当成好几次。
     *    去抖窗口需要"距上次边沿多久"这个信息，而 ISR 里既不该算时间
     *    （理由见上面第 1 条），也不该保存状态机。
     *    → 所以：ISR 只负责"报告引脚动了一下"，去抖和按下/松开判定
     *      全部交给主循环的 KeyService()（Core/SRC/app.cpp）。
     *      g_keyIrqCnt 会把抖动次数如实记下来，方便判断去抖窗口够不够。 */
    g_keyIrq = 1u;
    g_keyIrqCnt++;
    g_keyLevel = (LL_GPIO_IsInputPinSet(KEY_INT_Port, KEY_INT_Pin) != 0u) ? 1u : 0u;
  }

  /* USER CODE END EXTI0_1_IRQn 0 */
  /* USER CODE BEGIN EXTI0_1_IRQn 1 */

  /* USER CODE END EXTI0_1_IRQn 1 */
}

/**
 * @brief This function handles EXTI2_3_IRQn interrupt.
 * @param None
 * @retval None
 */
void EXTI2_3_IRQHandler(void)
{
  /* USER CODE BEGIN EXTI2_3_IRQn 0 */

  /* USER CODE END EXTI2_3_IRQn 0 */
  /* USER CODE BEGIN EXTI2_3_IRQn 1 */

  /* USER CODE END EXTI2_3_IRQn 1 */
}

/**
 * @brief This function handles EXTI4_15_IRQn interrupt.
 * @param None
 * @retval None
 */
void EXTI4_15_IRQHandler(void)
{
  /* USER CODE BEGIN EXTI4_15_IRQn 0 */

  /* 时钟芯片 (PB5/EXTI5) 的中断已移除 —— 时钟芯片将由独立例程处理。
   * 本工程不再配置 PB5 的 GPIO 和 EXTI5，此 handler 保留为空框架。 */

  /* USER CODE END EXTI4_15_IRQn 0 */
  /* USER CODE BEGIN EXTI4_15_IRQn 1 */

  /* USER CODE END EXTI4_15_IRQn 1 */
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
