/**
  ******************************************************************************
  * @file    app.h
  * @brief   应用层入口（C 可调用）
  *
  *  实现体是 C++（Core/SRC/app.cpp），这里只暴露一个 C 链接的入口，
  *  好让 PY32Studio 生成的 main.c 直接调用。
  ******************************************************************************
  */

#ifndef __APP_H__
#define __APP_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  应用主循环。**永不返回**。
 * @note   调用前必须已经完成 Studio_RCC_Init() / Studio_GPIO_Init() /
 *         Studio_DMA_Init() / Studio_I2C1_Init() / Studio_SPI1_Init()
 *         （main.c 里 Studio_*_Init() 那一段）。
 */
void App_Run(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_H__ */
