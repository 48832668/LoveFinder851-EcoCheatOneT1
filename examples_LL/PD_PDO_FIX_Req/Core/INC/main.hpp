/**
  ******************************************************************************
  * @file    main.hpp
  * @brief   桥接头：把 PY32Studio 生成的 C 头 main.h 暴露成 C++ 头 main.hpp
  *
  * ============================================================================
  *  为什么需要这个文件
  * ============================================================================
  *  共享库（LoveFinderLibForPY32_LL）的头文件是按 C++ 工程写的：
  *      FUSB302.hpp :  #include "main.hpp"
  *      st7735.hpp  :  #include "main.hpp"  /  #include "gpio.hpp"
  *  它们从 main.hpp 拿两样东西：
  *      ① LL 外设头（py32f0xx_ll_*.h）
  *      ② BSP_GetTick() 的声明
  *
  *  而本工程（PD_PDO_FIX_Req）的外设初始化是 **PY32Studio 生成的纯 C**，
  *  文件名是 main.h / gpio.h。两边文件名对不上。
  *
  *  处理办法有三种：
  *    (a) 把整个工程改成 C++（重命名 .c -> .cpp、.h -> .hpp）
  *        —— 就是 PD_PDO_List_Det 的做法。但那样 PY32Studio 一重新生成代码
  *           就会又冒出一份 main.c / main.h，和重命名后的文件打架。
  *    (b) 改共享库的 include —— 但 PD_PDO_List_Det 依赖 main.hpp，
  *        改了会把另一个工程弄坏。
  *    (c) ★ 本文件的办法：加一层**一行桥接头**。
  *
  *  选 (c)：库代码一个字都不用改，PY32Studio 重新生成代码也不会冲突。
  ******************************************************************************
  */

#ifndef __MAIN_HPP_BRIDGE__
#define __MAIN_HPP_BRIDGE__

/* PY32Studio 生成的 C 头：里面已经 include 了全部 LL 外设头。 */
#include "main.h"

/* uwTick / BSP_GetTick() / 三个中断标志。
 * 库里的 I2C、DMA 超时轮询都要用 BSP_GetTick()。 */
#include "irq_flags.h"

#endif /* __MAIN_HPP_BRIDGE__ */
