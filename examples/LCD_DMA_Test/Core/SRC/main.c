/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    main.c
  * @brief   LCD_DMA_Test - ST7735 DMA 性能演示（动画合集）
  *
  * 本示例演示 PY32F003 + ST7735 在 SPI1_TX->DMA1_Channel3 下的性能：
  *   - 双缓冲逐行渲染（CPU 计算与 DMA 传输重叠）
  *   - 彩虹渐变、流动波形、弹跳球、棋盘翻转、图形画廊、滚动字幕
  *   - 全屏 DMA 刷新 vs 阻塞刷新的 FPS 对比
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2026 Puya Semiconductor Co.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by Mcu Studio under BSD 3-Clause license,
  * the License ; You may not use this file except in compliance with the
  * License.You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "rcc.h"
#include "gpio.h"
#include "spi.h"
#include "dma.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "st7735.h"
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define DEMO_DURATION_MS  4000u
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
static uint32_t s_frame = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */
static void ShowSplash(void);
static void DemoRainbow(void);
static void DemoWaves(void);
static void DemoChecker(void);
static void DemoBall(void);
static void DemoGallery(void);
static void DemoMarquee(void);
static void DemoPerf(void);
/* USER CODE END PFP */

/* External functions --------------------------------------------------------*/
/* USER CODE BEGIN ExternalFunctions */

/* USER CODE END ExternalFunctions */


/* USER CODE BEGIN 0 */

/* HSV 色相(0..255) -> RGB565 */
static uint16_t HueToRGB565(uint8_t hue)
{
    uint8_t r = 0, g = 0, b = 0;
    uint8_t region = hue / 43;
    uint8_t rem    = (uint8_t)((hue - region * 43) * 6);

    switch (region)
    {
        case 0: r = 255;      g = rem;      b = 0;        break;
        case 1: r = 255 - rem; g = 255;     b = 0;        break;
        case 2: r = 0;         g = 255;     b = rem;      break;
        case 3: r = 0;         g = 255 - rem; b = 255;    break;
        case 4: r = rem;       g = 0;       b = 255;      break;
        default: r = 255;      g = 0;       b = 255 - rem; break;
    }
    return ST7735_RGB565(r, g, b);
}

/* 行渲染：彩虹渐变 */
static void RowRainbow(uint16_t y, uint8_t *dst, uint16_t w)
{
    uint16_t x;
    for (x = 0; x < w; x++)
    {
        uint8_t hue = (uint8_t)(x * 255U / w + y * 4U + (uint8_t)s_frame);
        ST7735_PUT565(dst, x, HueToRGB565(hue));
    }
}

/* 行渲染：流动波形 */
static void RowWaves(uint16_t y, uint8_t *dst, uint16_t w)
{
    uint16_t x;
    for (x = 0; x < w; x++)
    {
        uint8_t hue = (uint8_t)(x * 2U + y * 6U + (uint8_t)(s_frame * 2U));
        ST7735_PUT565(dst, x, HueToRGB565(hue));
    }
}

/* 行渲染：棋盘格翻转 */
static void RowChecker(uint16_t y, uint8_t *dst, uint16_t w)
{
    uint16_t x;
    uint8_t cell = (uint8_t)((y / 8U) & 1U);
    if ((s_frame / 12U) & 1U) cell = (uint8_t)(1U - cell);
    for (x = 0; x < w; x++)
    {
        uint8_t c = (uint8_t)(cell ^ (uint8_t)((x / 8U) & 1U));
        ST7735_PUT565(dst, x, c ? ST7735_WHITE : ST7735_ORANGE);
    }
}

/* USER CODE END 0 */

int main(void)
{
  /* USER CODE BEGIN 1 */
  /* USER CODE END 1 */
  /* MCU Configuration--------------------------------------------------------*/
  HAL_Init();
  /* USER CODE BEGIN Init */
  /* USER CODE END Init */

  /* Configure the system clock */
  Studio_RCC_Init();
  /* USER CODE BEGIN SysInit */
  /* USER CODE END SysInit */

  /* Initialize peripherals — DMA 必须先于 SPI */
  Studio_GPIO_Init();
  Studio_DMA_Init();
  Studio_SPI1_Init();
  /* USER CODE BEGIN 2 */
  ST7735_Init();

  while (1)
  {
    ShowSplash();
    DemoRainbow();
    DemoWaves();
    DemoChecker();
    DemoBall();
    DemoGallery();
    DemoMarquee();
    DemoPerf();
  }
  /* USER CODE END 2 */
  while(1) { }
}

/* USER CODE BEGIN 4 */

static void ShowSplash(void)
{
    uint32_t t0 = HAL_GetTick();
    s_frame = 0;

    ST7735_FillScreen_DMA(ST7735_NAVY);
    ST7735_DrawRoundRect(10, 8, ST7735_WIDTH - 20, 22, 4, ST7735_CYAN);
    ST7735_DrawString((ST7735_WIDTH - ST7735_StringWidth("PY32F003")) / 2, 12,
                      "PY32F003", ST7735_WHITE, ST7735_NAVY);
    ST7735_DrawString(14, 38, "LCD DMA DEMO", ST7735_YELLOW, ST7735_NAVY);
    ST7735_DrawString(14, 50, "160x80 ST7735", ST7735_GREEN, ST7735_NAVY);
    if (ST7735_IsDmaActive())
        ST7735_DrawString(14, 62, "TX: DMA1_CH3 (ACTIVE)", ST7735_GREEN, ST7735_NAVY);
    else
        ST7735_DrawString(14, 62, "TX: SPI (BLOCKING)", ST7735_RED, ST7735_NAVY);

    while (HAL_GetTick() - t0 < 2500u)
    {
        s_frame++;
        if ((s_frame & 4U) == 0)
            ST7735_DrawRect(4, 4, ST7735_WIDTH - 8, ST7735_HEIGHT - 8, ST7735_WHITE);
        else
            ST7735_DrawRect(4, 4, ST7735_WIDTH - 8, ST7735_HEIGHT - 8, ST7735_CYAN);
        HAL_Delay(30);
    }
}

static void DemoRainbow(void)
{
    uint32_t t0 = HAL_GetTick();
    while (HAL_GetTick() - t0 < DEMO_DURATION_MS)
    {
        ST7735_DrawFrame(RowRainbow);
        s_frame += 3U;
        HAL_Delay(16);
    }
}

static void DemoWaves(void)
{
    uint32_t t0 = HAL_GetTick();
    while (HAL_GetTick() - t0 < DEMO_DURATION_MS)
    {
        ST7735_DrawFrame(RowWaves);
        s_frame += 2U;
        HAL_Delay(16);
    }
}

static void DemoChecker(void)
{
    uint32_t t0 = HAL_GetTick();
    while (HAL_GetTick() - t0 < DEMO_DURATION_MS)
    {
        ST7735_DrawFrame(RowChecker);
        s_frame++;
        HAL_Delay(20);
    }
}

static void DemoBall(void)
{
    uint32_t t0 = HAL_GetTick();
    int16_t bx = 40, by = 20;
    int16_t vx = 2,  vy = 1;
    int16_t r  = 8;

    ST7735_FillScreen_DMA(ST7735_BLACK);
    ST7735_DrawString(8, 70, "BOUNCING BALL", ST7735_GRAY, ST7735_BLACK);

    uint16_t trail[16][2];
    uint8_t  trailCnt = 0;

    while (HAL_GetTick() - t0 < DEMO_DURATION_MS)
    {
        /* 擦除最旧拖尾 */
        if (trailCnt > 0)
        {
            ST7735_FillCircle(trail[0][0], trail[0][1], r, ST7735_BLACK);
            for (uint8_t i = 0; i + 1U < trailCnt; i++)
            {
                trail[i][0] = trail[i + 1][0];
                trail[i][1] = trail[i + 1][1];
            }
            trailCnt--;
        }

        bx += vx; by += vy;
        if (bx < r)            { bx = r;    vx = -vx; }
        if ((uint16_t)(bx + r) >= ST7735_WIDTH) { bx = (int16_t)(ST7735_WIDTH - r - 1); vx = -vx; }
        if (by < r)            { by = r;    vy = -vy; }
        if ((uint16_t)(by + r) >= ST7735_HEIGHT){ by = (int16_t)(ST7735_HEIGHT - r - 1); vy = -vy; }

        ST7735_FillCircle(bx, by, r, ST7735_RED);
        ST7735_DrawCircle(bx, by, r, ST7735_WHITE);
        trail[trailCnt][0] = (uint16_t)bx;
        trail[trailCnt][1] = (uint16_t)by;
        if (trailCnt < 16) trailCnt++;
        HAL_Delay(16);
    }
}

static void DemoGallery(void)
{
    uint32_t t0 = HAL_GetTick();
    ST7735_FillScreen_DMA(ST7735_BLACK);
    ST7735_DrawString(8, 72, "GRAPHICS PRIMITIVES", ST7735_GRAY, ST7735_BLACK);

    /* 圆 */
    ST7735_DrawCircle(20, 20, 12, ST7735_RED);
    ST7735_DrawCircle(50, 20, 12, ST7735_GREEN);
    ST7735_DrawCircle(80, 20, 12, ST7735_BLUE);
    ST7735_DrawCircle(110, 20, 12, ST7735_YELLOW);

    /* 实心圆 */
    ST7735_FillCircle(20, 50, 10, ST7735_ORANGE);
    ST7735_FillCircle(45, 50, 10, ST7735_MAGENTA);
    ST7735_FillCircle(70, 50, 10, ST7735_GREEN);
    ST7735_FillCircle(95, 50, 10, ST7735_BLUE);

    /* 矩形 + 圆角矩形 */
    ST7735_DrawRect(110, 42, 20, 14, ST7735_WHITE);
    ST7735_DrawRoundRect(135, 42, 20, 14, 4, ST7735_WHITE);
    ST7735_FillRoundRect(110, 60, 20, 12, 4, ST7735_CYAN);
    ST7735_FillRect(135, 60, 20, 12, ST7735_YELLOW);

    /* 斜线 */
    ST7735_DrawLine(10, 35, 155, 0, ST7735_GRAY);
    ST7735_DrawLine(155, 35, 10, 0, ST7735_GRAY);
    HAL_Delay(2000);

    ST7735_FillScreen_DMA(ST7735_BLACK);
    ST7735_DrawString(8, 72, "DMA PING-PONG STREAM", ST7735_GRAY, ST7735_BLACK);
    /* 文字 + 滚动 */
    ST7735_DrawString(10, 10, "Hello DMA!", ST7735_CYAN, ST7735_BLACK);
    ST7735_DrawString(10, 20, "SPI TX:", ST7735_WHITE, ST7735_BLACK);
    ST7735_DrawString(70, 20, "DMA1_CH3", ST7735_GREEN, ST7735_BLACK);
    ST7735_DrawString(10, 30, "Freq:", ST7735_WHITE, ST7735_BLACK);
    ST7735_DrawString(50, 30, "4 MHz", ST7735_YELLOW, ST7735_BLACK);
    HAL_Delay(2000);

    while (HAL_GetTick() - t0 < DEMO_DURATION_MS)
    {
        HAL_Delay(10);
    }
}

static void DemoMarquee(void)
{
    static const char msg[] = "PY32F003 DMA LCD - 160x80 - Code:v1.0 ";
    uint32_t t0 = HAL_GetTick();
    int16_t x = (int16_t)ST7735_WIDTH;

    while (HAL_GetTick() - t0 < DEMO_DURATION_MS)
    {
        ST7735_FillScreen_DMA(ST7735_BLACK);
        ST7735_DrawHLine(0, 38, ST7735_WIDTH, ST7735_NAVY);
        ST7735_DrawString((uint16_t)x, 40, msg, ST7735_YELLOW, ST7735_BLACK);
        x--;
        if (x < -(int16_t)ST7735_StringWidth(msg))
            x = (int16_t)ST7735_WIDTH;
        HAL_Delay(8);
    }
}

static void DemoPerf(void)
{
    char buf[48];
    uint32_t t0, framesDMA, framesBlock, fpsDMA, fpsBlock;
    uint8_t flip = 0;

    /* DMA 全屏刷新 1s */
    t0 = HAL_GetTick(); framesDMA = 0;
    while (HAL_GetTick() - t0 < 1000u)
    {
        ST7735_FillScreen_DMA(flip ? ST7735_BLUE : ST7735_BLACK);
        flip ^= 1; framesDMA++;
    }
    fpsDMA = framesDMA;

    /* 阻塞全屏刷新 1s */
    t0 = HAL_GetTick(); framesBlock = 0; flip = 0;
    while (HAL_GetTick() - t0 < 1000u)
    {
        ST7735_FillScreen(flip ? ST7735_BLUE : ST7735_BLACK);
        flip ^= 1; framesBlock++;
    }
    fpsBlock = framesBlock;

    ST7735_FillScreen_DMA(ST7735_BLACK);
    ST7735_DrawString(8, 8, "PERF TEST (1s each):", ST7735_WHITE, ST7735_BLACK);

    ST7735_DrawString(8, 22, "DMA  :", ST7735_CYAN, ST7735_BLACK);
    sprintf(buf, "%lu FPS (%lu ms/f)", (unsigned long)fpsDMA,
            (unsigned long)(fpsDMA ? (1000u / fpsDMA) : 0u));
    ST7735_DrawString(52, 22, buf, ST7735_GREEN, ST7735_BLACK);

    ST7735_DrawString(8, 34, "BLOCK:", ST7735_CYAN, ST7735_BLACK);
    sprintf(buf, "%lu FPS (%lu ms/f)", (unsigned long)fpsBlock,
            (unsigned long)(fpsBlock ? (1000u / fpsBlock) : 0u));
    ST7735_DrawString(52, 34, buf, ST7735_RED, ST7735_BLACK);

    ST7735_DrawString(8, 50, "SPI:4MHz DMA1_CH3", ST7735_GRAY, ST7735_BLACK);
    ST7735_DrawString(8, 60, "Ping-pong row render", ST7735_GRAY, ST7735_BLACK);

    HAL_Delay(3000);
}

/* USER CODE END 4 */

void Error_Handler(void)
{
  __disable_irq();
  while (1) { }
}

#ifdef  USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  while (1) { }
}
#endif /* USE_FULL_ASSERT */