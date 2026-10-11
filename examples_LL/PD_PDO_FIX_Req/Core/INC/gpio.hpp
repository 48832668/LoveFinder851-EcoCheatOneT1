/**
  ******************************************************************************
  * @file    gpio.hpp
  * @brief   桥接头：把 PY32Studio 生成的 C 头 gpio.h 暴露成 C++ 头 gpio.hpp
  *
  *  st7735.hpp 里写的是 #include "gpio.hpp"，目的是拿板级引脚宏：
  *      LCD_CS_Pin / LCD_DC_Pin / LCD_RESET_Pin / LCD_EN_Pin
  *  它们在本工程的 gpio.h 里已经定义好了，这里只做一次转发。
  *
  *  详细理由见 main.hpp 顶部的说明。
  ******************************************************************************
  */

#ifndef __GPIO_HPP_BRIDGE__
#define __GPIO_HPP_BRIDGE__

#include "gpio.h"

#endif /* __GPIO_HPP_BRIDGE__ */
