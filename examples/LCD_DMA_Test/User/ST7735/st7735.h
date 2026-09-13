/**
 * @file st7735.h
 * @brief ST7735 160x80 (0.96 inch) LCD driver - DMA accelerated version
 *
 * 与 examples/LCD_Test 的阻塞版驱动相比，本驱动新增：
 *   1. SPI1_TX -> DMA1_Channel3 的 DMA 传输通道；
 *   2. 双缓冲逐行渲染管线 ST7735_DrawFrame()：
 *      CPU 计算第 N+1 行时，DMA 正在把第 N 行推给屏幕（计算与传输并行）；
 *   3. 常用图形原语（点/线/矩形/圆/圆角矩形）与 RGB565 颜色工具。
 *
 * 硬件：SPI1 (PA3=MOSI, PA5=SCK) + GPIO 控制线（定义见 gpio.h）：
 *   LCD_DC=PA4, LCD_RESET=PA6, LCD_CS=PA7, LCD_EN(背光)=PA12
 */

#ifndef ST7735_H
#define ST7735_H

#include "main.h"
#include "st7735_config.h"

/*===========================================================================
 * RGB565 颜色
 *===========================================================================*/
#define ST7735_BLACK     0x0000
#define ST7735_WHITE     0xFFFF
#define ST7735_RED       0xF800
#define ST7735_GREEN     0x07E0
#define ST7735_BLUE      0x001F
#define ST7735_CYAN      0x07FF
#define ST7735_MAGENTA   0xF81F
#define ST7735_YELLOW    0xFFE0
#define ST7735_ORANGE    0xFC00
#define ST7735_GRAY      0x8410
#define ST7735_DARKGRAY  0x4208
#define ST7735_NAVY      0x000F
#define ST7735_PURPLE    0x781F
#define ST7735_BROWN     0xA145

/* RGB888 -> RGB565 */
#define ST7735_RGB565(r, g, b) \
    ((uint16_t)((((uint16_t)(r) & 0xF8U) << 8) | \
                (((uint16_t)(g) & 0xFCU) << 3) | \
                 ((uint16_t)(b) >> 3)))

/*===========================================================================
 * 初始化 / 背光
 *===========================================================================*/
void ST7735_Init(void);
void ST7735_SetBacklight(uint8_t on);

/*===========================================================================
 * 阻塞式绘制（小面积 / 单点，走 CPU 阻塞 SPI）
 *===========================================================================*/
void ST7735_FillScreen(uint16_t color);
void ST7735_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void ST7735_DrawPixel(uint16_t x, uint16_t y, uint16_t color);
void ST7735_DrawHLine(uint16_t x, uint16_t y, uint16_t w, uint16_t color);
void ST7735_DrawVLine(uint16_t x, uint16_t y, uint16_t h, uint16_t color);
void ST7735_DrawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void ST7735_DrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);
void ST7735_DrawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color);
void ST7735_FillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color);
void ST7735_DrawRoundRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t r, uint16_t color);
void ST7735_FillRoundRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t r, uint16_t color);

/*===========================================================================
 * 文字（5x7 ASCII 字体，字符 0x20..0x7E）
 *===========================================================================*/
void ST7735_DrawChar(uint16_t x, uint16_t y, char ch, uint16_t color, uint16_t bg);
void ST7735_DrawString(uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t bg);
uint16_t ST7735_StringWidth(const char *str);

/*===========================================================================
 * DMA 加速接口（SPI1_TX -> DMA1_Channel3）
 *===========================================================================*/
void ST7735_FillRect_DMA(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void ST7735_FillScreen_DMA(uint16_t color);
void ST7735_Blit565_DMA(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *pix);

#define ST7735_PUT565(dst, i, color)                       \
    do {                                                   \
        (dst)[(i) * 2]     = (uint8_t)((color) >> 8);      \
        (dst)[(i) * 2 + 1] = (uint8_t)((color) & 0xFFU);   \
    } while (0)

typedef void (*ST7735_RowRenderFn)(uint16_t y, uint8_t *dst, uint16_t w);
void ST7735_DrawFrame(ST7735_RowRenderFn fn);

#endif /* ST7735_H */