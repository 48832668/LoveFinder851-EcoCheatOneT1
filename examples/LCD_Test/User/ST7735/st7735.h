/**
 * @file st7735.h
 * @brief ST7735 160x80 (0.96 inch) LCD driver - C implementation, blocking SPI
 *
 * Same display controller as the LoveFinder491_PowerOneT2 (POT2) project.
 * Uses SPI1 (PA3=MOSI, PA5=SCK) plus GPIO control lines defined in gpio.h:
 *   LCD_DC=PA4, LCD_RESET=PA6, LCD_CS=PA7, LCD_EN(backlight)=PA12
 */

#ifndef ST7735_H
#define ST7735_H

#include "main.h"
#include "st7735_config.h"

/* RGB565 colors */
#define ST7735_BLACK     0x0000
#define ST7735_WHITE     0xFFFF
#define ST7735_RED       0xF800
#define ST7735_GREEN     0x07E0
#define ST7735_BLUE      0x001F
#define ST7735_CYAN      0x07FF
#define ST7735_MAGENTA   0xF81F
#define ST7735_YELLOW    0xFFE0
#define ST7735_ORANGE    0xFC00

void ST7735_Init(void);
void ST7735_FillScreen(uint16_t color);
void ST7735_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void ST7735_DrawChar(uint16_t x, uint16_t y, char ch, uint16_t color, uint16_t bg);
void ST7735_DrawString(uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t bg);

#endif /* ST7735_H */
