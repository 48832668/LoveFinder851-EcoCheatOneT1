/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    py32f003_it.cpp
  * @brief   This file provides code for the configuration
  *          of all used NVIC.
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
#include "py32f003_it.hpp"
/* USER CODE BEGIN Includes */
#include "gpio.hpp"
/* USER CODE END Includes */

/* USER CODE BEGIN EV */
/* FUSB302 的中断标志（定义在 main.cpp 的 extern "C" 块里，供 OpenOCD 直接读）。
 *
 * ⚠️ ISR 与主循环之间**只通过这一个字节握手**，原因见下面 EXTI0_1_IRQHandler
 *    里那段说明 —— 千万不要在这个文件里加 I²C 调用。
 *
 * ⚠️ 必须自己包一层 extern "C"：main.cpp 里那两个量是 C 链接的，
 *    而这里的 USER CODE BEGIN EV 区在下面那个 extern "C" { 之前，
 *    不包的话会按 C++ 规则做名字修饰 → 链接期 undefined symbol。 */
extern "C" {
extern volatile uint8_t  g_pdIrq;     /* ISR 置 1，主循环消费后清 0 */
extern volatile uint32_t g_pdIrqCnt;  /* ISR 累计进入次数（诊断） */
}
/* USER CODE END EV */

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

/* External functions --------------------------------------------------------*/
/* USER CODE BEGIN EV */

/* USER CODE END EV */

/*****************************************************************************/
/*           Cortex-M Processor Interruption and Exception Handlers          */
/*****************************************************************************/

/*
 * 为什么这里必须 extern "C"：
 *   启动文件 startup_py32f003xx.s 用 `IMPORT SysTick_Handler` 这类汇编符号
 *   按【名字】引用中断入口。C++ 默认会做名字修饰（name mangling），
 *   不加 extern "C" 会导出成 _Z15SysTick_Handlerv 之类，汇编端找不到
 *   → 链接报 undefined symbol，或者向量表里是空指针。
 *   这是硬件 ABI 层面的要求，与"是否兼容 C"无关。
 */
extern "C" {

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
  uwTick++;
  /* USER CODE BEGIN SysTick_Handler 1 */

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

  /* USER CODE END EXTI0_1_IRQn 0 */
  if (LL_EXTI_IsActiveFlag(LL_EXTI_LINE_0) != 0U)
  {
    LL_EXTI_ClearFlag(LL_EXTI_LINE_0);

    /* PA0 FUSB_INT —— FUSB302 的 INT_N（开漏、低有效，下降沿触发）。
     *
     * ★★ 这里只置一个标志，**绝对不做 I²C**。三个理由，缺一不可：
     *
     *   1) 会掐死时基。库里每次 I²C 访问都带 FUSB302_I2C_TIMEOUT_MS=20ms 的
     *      超时轮询，而本 ISR 的优先级是 0（见 Studio_GPIO_Init）。在优先级 0
     *      里阻塞 20ms，SysTick（优先级更低）就进不来 → uwTick 停走 →
     *      整个工程所有 BSP_GetTick() 计时（先听后催、tSenderResponse 那
     *      24ms 红线、VBUS 去抖）全部失真。
     *   2) 会撕裂总线。I²C1 是主循环和这里共用的；主循环可能正卡在一次
     *      多字节传输中间，ISR 插进来重入 I²C 外设会直接写坏时序。
     *   3) 没必要。中断的全部价值是"立刻知道有事"，而真正的处理
     *      （读 FIFO、回 Request）本来就必须在主循环里按 PD 状态机来做。
     *
     * 主循环消费完这个标志后，还会回读 PA0 电平决定要不要再来一轮 ——
     * 这样"服务期间又来了新报文"（INT_N 本来就是低的，不会再产生下降沿）
     * 也不会漏，详见 App_Run() 循环末尾的"重新武装"段。 */
    g_pdIrq = 1u;
    g_pdIrqCnt++;
    /* PA0 FUSB_INT */
  }
  if (LL_EXTI_IsActiveFlag(LL_EXTI_LINE_1) != 0U)
  {
    LL_EXTI_ClearFlag(LL_EXTI_LINE_1);
    /* PA1 KEY_INT */
  }
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

  /* USER CODE END EXTI4_15_IRQn 0 */
  if (LL_EXTI_IsActiveFlag(LL_EXTI_LINE_5) != 0U)
  {
    LL_EXTI_ClearFlag(LL_EXTI_LINE_5);
    /* PB5 CLK_INT */
  }
  /* USER CODE BEGIN EXTI4_15_IRQn 1 */

  /* USER CODE END EXTI4_15_IRQn 1 */
}

} /* extern "C" */

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
