/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    main.cpp
  * @brief   This file provides code for the configuration
  *          of all used MAIN.
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
#include "main.hpp"
#include "rcc.hpp"
#include "gpio.hpp"
#include "i2c.hpp"
#include "spi.hpp"
#include "usart.hpp"
#include "tim.hpp"
#include "crc.hpp"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "st7735.hpp"
#include <cstdio>      /* snprintf */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* 每个测试环节的持续时间 (ms) */
#define DEMO_DURATION_MS   4000u

/* 性能测量窗口：阻塞 / DMA 各测这么久，用于最后的对比 */
#define PERF_WINDOW_MS     1000u

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* 1 ms tick counter, incremented from SysTick_Handler() */
volatile uint32_t uwTick = 0;
/* USER CODE BEGIN PV */
/* 全局帧计数：驱动动画的时间轴 */
static uint32_t s_frame = 0;

/* 各环节的测量结果（环节 4 的汇总表用）
   —— 每个环节测两遍：先 DMA 关（阻塞），再 DMA 开。口径完全一致。 */
static const char* s_stageName[3] = { "FILL", "SHAPE", "FLAG" };
static uint32_t    s_cpuFps[3]    = { 0U, 0U, 0U };   /* DMA 关 每秒帧数 */
static uint32_t    s_cpuMs[3]     = { 0U, 0U, 0U };   /* DMA 关 每帧毫秒 */
static uint32_t    s_dmaFps[3]    = { 0U, 0U, 0U };   /* DMA 开 每秒帧数 */
static uint32_t    s_dmaMs[3]     = { 0U, 0U, 0U };   /* DMA 开 每帧毫秒 */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */
/* 测试流程的 4 个环节，顺序执行。
 * 每个测试环节内部：状态栏标路径 -> DMA 关测 1 秒 -> DMA 开测 1 秒
 *                 -> 清屏 -> 本环节小结 */
static void Stage1_Fill(void);      /* 1. 大块填充对比 */
static void Stage2_Shape(void);     /* 2. 随机几何图形对比 */
static void Stage3_Flag(void);      /* 3. HelloWorld + 红旗飘扬（计算密集） */
static void Stage4_Summary(void);   /* 4. 总汇总       */

/* USER CODE END PFP */

/* External functions --------------------------------------------------------*/
/* USER CODE BEGIN ExternalFunctions */

/* USER CODE END ExternalFunctions */

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */
  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  LL_APB1_GRP2_EnableClock(LL_APB1_GRP2_PERIPH_SYSCFG);
  LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_PWR);
  LL_PWR_EnableBkUpAccess();
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock (HSI 8 MHz) and the 1 ms SysTick time base */
  Studio_RCC_Init();
  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals
     （GPIO / I2C1 / SPI1 / USART1 / TIM3 / CRC）
     SPI1_TX 的 DMA 配置在 Studio_SPI1_Init() 内部完成 ——
     与 PyStudio 的 .pysprj（SPI1.DMA.body.SPI1_TX）一致，没有独立的 DMA 初始化。 */
  Studio_GPIO_Init();
  Studio_I2C1_Init();
  Studio_SPI1_Init();
  Studio_USART1_Init();
  Studio_TIM3_Init();
  Studio_CRC_Init();
  /* USER CODE BEGIN 2 */
  /* ST7735 初始化（库内会做一次 DMA 启动自检，失败则永久退化为阻塞 SPI） */
  ST7735_Init();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */
    /* USER CODE BEGIN 3 */
    /* ====================================================================
     * 测试流程 —— 4 个环节顺序执行，然后循环
     *
     * 屏幕下半部分成左右两块：左边 CPU（阻塞 SPI），右边 DMA（DMA1_Channel3）。
     * 每个环节都做同一件事两遍 —— 先在左半用 CPU，再在右半用 DMA ——
     * 然后清屏，单独显示本环节的性能小结。
     *
     *   1  FILL     大块填充      每帧一次 80x50 传输
     *   2  ROWS     逐行渐变      每帧 50 行
     *   3  SHAPES   图形原语      圆 + 圆角矩形，每帧几十段填充
     *   4  SUMMARY  总汇总        三个环节的 CPU / DMA / 加速比
     * ==================================================================== */
    Stage1_Fill();
    Stage2_Shape();
    Stage3_Flag();
    Stage4_Summary();
  }
  /* USER CODE END 3 */
}

/**
  * @brief  Return the 1 ms tick counter maintained by SysTick_Handler().
  * @note   LL_mDelay() only polls the SysTick COUNTFLAG and keeps no counter,
  *         so this is the millisecond time base for application code.
  * @retval Milliseconds since reset
  */
uint32_t BSP_GetTick() noexcept
{
  return uwTick;
}

/* USER CODE BEGIN 4 */

/*============================================================================
 * 屏幕布局 —— 不再左右分屏
 *
 *   ┌─────────────────────────────────────────────┐ y=0
 *   │ [n/4] 环节名                                 │  状态栏 (0..19)
 *   │ DMA:ON (绿) / DMA:OFF (红)   measuring...     │
 *   ├─────────────────────────────────────────────┤ y=20
 *   │                                              │
 *   │         全屏测试区 (160 x 60)                 │  整块都用
 *   │        FILL / SHAPE / FLAG                   │  CPU 或 DMA
 *   │                                              │
 *   └─────────────────────────────────────────────┘ y=80
 *
 * 每个环节测两遍：先 DMA 关（阻塞 SPI），再 DMA 开。
 * 上方状态栏用【绿 DMA:ON / 红 DMA:OFF】标明当前测量的是哪条路径。
 *============================================================================*/
namespace {
constexpr uint16_t BAR_H  = 20;                        /* 状态栏高度 0..19 */
constexpr uint16_t BODY_Y = BAR_H;                     /* 测试区起点 20 */
constexpr uint16_t BODY_H = ST7735_HEIGHT - BODY_Y;    /* 60 */
constexpr uint16_t BODY_W = ST7735_WIDTH;              /* 160 */

constexpr uint8_t  STAGE_TOTAL = 4U;                   /* 3 个测试环节 + 汇总 */

/* 本演示用到的颜色 */
constexpr uint16_t C_GRAY  = 0x8410;
constexpr uint16_t C_BARBG = 0x10A2;   /* 状态栏底色：深灰蓝 */
}

/* 统一文字输出：字体用字符级子集 Font_7x10 */
static void Text(uint16_t x, uint16_t y, const char* s, uint16_t fg, uint16_t bg)
{
  ST7735_WriteString(x, y, s, Font_7x10, fg, bg);
}

/*--- 状态栏 ---------------------------------------------------------------
 * 第一行：[n/4] 环节名
 * 第二行：DMA 状态 —— 开=绿 DMA:ON / 关=红 DMA:OFF，后面跟测量提示
 * 每次开始一段测量前调用，把当前路径标清楚。
 *--------------------------------------------------------------------------*/
static void StatusBegin(uint8_t stage, const char* name, bool dmaOn)
{
  char buf[24];

  ST7735_FillScreen(ST7735::BLACK);
  ST7735_FillRectangle(0, 0, ST7735_WIDTH, BAR_H, C_BARBG);

  (void)snprintf(buf, sizeof(buf), "[%u/%u] %s",
                 static_cast<unsigned>(stage), static_cast<unsigned>(STAGE_TOTAL), name);
  Text(2, 1, buf, ST7735::WHITE, C_BARBG);

  /* 第二行：DMA 开/关用颜色区分 —— 这就是本环节当前测的是哪条路径 */
  if (dmaOn)
  {
    Text(2, 11, "DMA:ON", ST7735::GREEN, C_BARBG);
  }
  else
  {
    Text(2, 11, "DMA:OFF", ST7735::RED, C_BARBG);
  }
  Text(42, 11, "measuring...", C_GRAY, C_BARBG);
}

/*--- 本环节性能小结（清屏后单独显示） --------------------------------------*/
static void StageSummary(uint8_t stage, const char* name,
                         uint32_t cpuFps, uint32_t cpuMs,
                         uint32_t dmaFps, uint32_t dmaMs)
{
  char buf[40];
  const uint32_t sp10 = (cpuFps != 0U) ? ((dmaFps * 10U) / cpuFps) : 0U;

  /* 记入汇总表（环节 1..3 对应下标 0..2） */
  if (stage >= 1U && stage <= 3U)
  {
    const uint8_t i = static_cast<uint8_t>(stage - 1U);
    s_stageName[i] = name;
    s_cpuFps[i] = cpuFps;
    s_cpuMs[i]  = cpuMs;
    s_dmaFps[i] = dmaFps;
    s_dmaMs[i]  = dmaMs;
  }

  ST7735_FillScreen(ST7735::BLACK);
  ST7735_FillRectangle(0, 0, ST7735_WIDTH, BAR_H, C_BARBG);
  (void)snprintf(buf, sizeof(buf), "[%u] %s  RESULT",
                 static_cast<unsigned>(stage), name);
  Text(2, 1, buf, ST7735::WHITE, C_BARBG);

  Text(2, 11, ST7735_IsDmaActive() ? "SPI 12MHz  DMA:ON" : "SPI 12MHz  DMA:OFF",
       ST7735_IsDmaActive() ? ST7735::GREEN : ST7735::RED, C_BARBG);

  Text(4, 28, "noDMA", ST7735::CYAN, ST7735::BLACK);
  (void)snprintf(buf, sizeof(buf), "%lu FPS   %lu ms",
                 static_cast<unsigned long>(cpuFps), static_cast<unsigned long>(cpuMs));
  Text(44, 28, buf, ST7735::RED, ST7735::BLACK);

  Text(4, 42, "DMA", ST7735::YELLOW, ST7735::BLACK);
  (void)snprintf(buf, sizeof(buf), "%lu FPS   %lu ms",
                 static_cast<unsigned long>(dmaFps), static_cast<unsigned long>(dmaMs));
  Text(44, 42, buf, ST7735::GREEN, ST7735::BLACK);

  (void)snprintf(buf, sizeof(buf), "Speedup  %lu.%lu x",
                 static_cast<unsigned long>(sp10 / 10U),
                 static_cast<unsigned long>(sp10 % 10U));
  Text(4, 58, buf, ST7735::WHITE, ST7735::BLACK);

  (void)snprintf(buf, sizeof(buf), "tx:%lu f:%lu rm:%lu",
                 static_cast<unsigned long>(ST7735_GetDmaTxOk()),
                 static_cast<unsigned long>(ST7735_GetDmaTxFail()),
                 static_cast<unsigned long>(ST7735_GetDmaRemapReg() & 0x1FU));
  Text(4, 70, buf, C_GRAY, ST7735::BLACK);

  LL_mDelay(2500);
}

/*============================================================================
 * 简单 LCG 伪随机数 —— 无除法、无浮点（Cortex-M0+ 没有硬件除法器）
 * 用高位缩放得到 [0,m)，避免 __aeabi_uidiv 软除法。
 *==========================================================================*/
static uint32_t s_rng = 0x1234567U;
static uint32_t rnd(void)
{
  s_rng = s_rng * 1664525U + 1013904223U;
  return s_rng;
}
static uint16_t rnd16(uint16_t m)   /* [0,m)，m>0 */
{
  return static_cast<uint16_t>(((rnd() >> 16) * static_cast<uint32_t>(m)) >> 16);
}

/*============================================================================
 * 几何图形集合 —— 每帧随机生成一组，用行渲染器画进帧缓冲
 *
 * 点测试全用整数比较/乘方，无除法无浮点。
 * 渲染时按集合顺序覆盖，后画的图形压在前面 —— 形成随机叠加画面。
 * 每次画完一整帧（=每帧输出完成后），下一帧重新随机 → 相当于清屏重画。
 *==========================================================================*/
enum ShapeType : uint8_t {
  S_CIRCLE, S_RECT, S_ROUNDRECT, S_TRIANGLE, S_DIAMOND
};
struct Shape {
  uint8_t  type;
  int16_t  x0, y0, x1, y1;   /* 包围盒 */
  int16_t  cx, cy, r;        /* 中心 / 半径 */
  uint16_t color;
};
#define SHAPE_COUNT 6U
static Shape g_shapes[SHAPE_COUNT];

static void ShapeGen(void)
{
  for (uint8_t i = 0; i < SHAPE_COUNT; i++)
  {
    Shape& s = g_shapes[i];
    s.type  = static_cast<uint8_t>(rnd16(5U));                 /* 0..4 */
    s.color = ST7735::Color565(
        static_cast<uint8_t>(128U + rnd16(128U)),
        static_cast<uint8_t>(rnd16(256U)),
        static_cast<uint8_t>(rnd16(256U)));
    s.cx = static_cast<int16_t>(rnd16(BODY_W));
    s.cy = static_cast<int16_t>(rnd16(BODY_H));
    s.r  = static_cast<int16_t>(4 + rnd16(20));

    /* 以 (cx,cy) 为中心、半径 r 生成包围盒，并裁剪到屏幕内 */
    int16_t bx0 = s.cx - s.r, by0 = s.cy - s.r;
    int16_t bx1 = s.cx + s.r, by1 = s.cy + s.r;
    if (bx0 < 0) bx0 = 0;
    if (bx1 >= (int16_t)BODY_W) bx1 = (int16_t)BODY_W - 1;
    if (by0 < 0) by0 = 0;
    if (by1 >= (int16_t)BODY_H) by1 = (int16_t)BODY_H - 1;
    s.x0 = bx0; s.y0 = by0; s.x1 = bx1; s.y1 = by1;
  }
}

/* 三角形半平面符号（无捕获、纯整数） */
static int32_t sgnf(int32_t px, int32_t py, int32_t qx, int32_t qy, int32_t rx, int32_t ry)
{
  return (qx - px) * (ry - py) - (qy - py) * (rx - px);
}

/* 点是否落在图形内 */
static bool ShapeHit(const Shape& s, int16_t x, int16_t y)
{
  switch (s.type)
  {
    case S_CIRCLE: {
      int32_t dx = x - s.cx, dy = y - s.cy;
      return (dx * dx + dy * dy) <= (int32_t)s.r * s.r;
    }
    case S_RECT:
      return (x >= s.x0 && x <= s.x1 && y >= s.y0 && y <= s.y1);
    case S_ROUNDRECT: {
      const int16_t r = (s.r < 8) ? s.r : 8;   /* 圆角半径上限 8 */
      if (!(x >= s.x0 && x <= s.x1 && y >= s.y0 && y <= s.y1)) return false;
      int16_t ccx = (x < s.cx) ? (s.x0 + r) : (s.x1 - r);
      int16_t ccy = (y < s.cy) ? (s.y0 + r) : (s.y1 - r);
      int32_t dx = x - ccx, dy = y - ccy;
      return (dx * dx + dy * dy) <= (int32_t)r * r;
    }
    case S_TRIANGLE: {
      int16_t v1x = s.cx,   v1y = s.y0;
      int16_t v2x = s.x1,   v2y = s.y1;
      int16_t v3x = s.x0,   v3y = s.y1;
      int32_t d1 = sgnf(x, y, v1x, v1y, v2x, v2y);
      int32_t d2 = sgnf(x, y, v2x, v2y, v3x, v3y);
      int32_t d3 = sgnf(x, y, v3x, v3y, v1x, v1y);
      bool neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
      bool pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
      return !(neg && pos);
    }
    default: {   /* S_DIAMOND：|x-cx|+|y-cy| <= r */
      int32_t dx = x - s.cx; if (dx < 0) dx = -dx;
      int32_t dy = y - s.cy; if (dy < 0) dy = -dy;
      return (dx + dy) <= s.r;
    }
  }
}

/* 行渲染器：把随机图形画进第 y 行 */
static void ShapeRowRender(uint16_t y, uint8_t* dst, uint16_t w)
{
  const int16_t yy = static_cast<int16_t>(y - BODY_Y);
  for (uint16_t x = 0; x < w; x++)
  {
    uint16_t col = ST7735::BLACK;
    for (uint8_t i = 0; i < SHAPE_COUNT; i++)
    {
      if (ShapeHit(g_shapes[i], static_cast<int16_t>(x), yy)) col = g_shapes[i].color;
    }
    ST7735_PUT565(dst, x, col);
  }
}

/*============================================================================
 * 红旗飘扬 + HelloWorld
 *
 * 背景：红色，亮度按行/列/时间的三角波流动 —— 看起来像红旗在飘。
 * 文字：HelloWorld 永远画在最上层，绝不被背景覆盖。
 *   —— 文字像素由字库位图逐点判定，只要落在字模上就覆盖背景色。
 *
 * 文字位置：10 个字符 × 7px = 70px 宽，垂直居中。Font_7x10 高 10px。
 *==========================================================================*/
static const char   kText[]   = "HelloWorld";
constexpr uint8_t   TEXT_COLS = 10;
constexpr int16_t   TEXT_W    = 7 * TEXT_COLS;              /* 70 */
constexpr int16_t   TEXT_X    = (ST7735_WIDTH - TEXT_W) / 2; /* 45 */
constexpr int16_t   TEXT_Y    = (BODY_H - 10) / 2;           /* body 内 25 */
constexpr uint16_t  TEXT_FG   = ST7735::WHITE;

/* 返回 true 表示 (x,y)（body 局部坐标）落在 HelloWorld 字模上，颜色写入 *col */
static bool TextPixel(int16_t x, int16_t y, uint16_t* col)
{
  if (y < TEXT_Y || y >= TEXT_Y + 10) return false;
  const int16_t row = y - TEXT_Y;
  for (uint8_t c = 0; c < TEXT_COLS; c++)
  {
    const int16_t cx0 = TEXT_X + static_cast<int16_t>(c) * 7;
    if (x < cx0 || x >= cx0 + 7) continue;
    const uint16_t* g = font_get_glyph(Font_7x10, static_cast<uint8_t>(kText[c]));
    if (g == nullptr) continue;
    const int16_t bx = x - cx0;               /* 0..6 */
    if ((g[row] << bx) & 0x8000) { *col = TEXT_FG; return true; }
    return false;                             /* 命中该字符但该列空白 */
  }
  return false;
}

/* 行渲染器：红旗 + HelloWorld（文字始终在最上层） */
static void FlagRowRender(uint16_t y, uint8_t* dst, uint16_t w)
{
  const int16_t yy = static_cast<int16_t>(y - BODY_Y);
  const uint8_t t  = static_cast<uint8_t>(s_frame & 0x3FU);

  for (uint16_t x = 0; x < w; x++)
  {
    uint16_t col;

    /* 三角波亮度：沿 (x*3 + y*5) 方向流动，随时间 t 平移 */
    int32_t ph = static_cast<int32_t>(x) * 3 + static_cast<int32_t>(yy) * 5
                 - static_cast<int32_t>(t) * 2;
    uint8_t wave = static_cast<uint8_t>(
        ((ph & 0x7F) < 0x40) ? (ph & 0x3F) : (0x7F - (ph & 0x3F)));

    /* 红旗基色 + 流动亮度 */
    const uint8_t r = static_cast<uint8_t>(0xC0U + (wave >> 1));  /* 192..255 红 */
    const uint8_t g = static_cast<uint8_t>(wave >> 2);
    const uint8_t b = static_cast<uint8_t>(wave >> 3);
    col = ST7735::Color565(r, g, b);

    /* 文字覆盖（永不遮盖） */
    uint16_t tc;
    if (TextPixel(static_cast<int16_t>(x), yy, &tc)) col = tc;

    ST7735_PUT565(dst, x, col);
  }
}

/*============================================================================
 * 环节 1/4：大块填充 —— 全屏一块，最能体现 DMA 价值
 *==========================================================================*/
static const uint16_t kFillPalette[] = {
  ST7735::BLACK, ST7735::RED,  ST7735::ORANGE, ST7735::YELLOW,
  ST7735::GREEN, ST7735::CYAN, ST7735::BLUE,   ST7735::MAGENTA
};
constexpr uint8_t kFillPaletteLen =
    static_cast<uint8_t>(sizeof(kFillPalette) / sizeof(kFillPalette[0]));

static void Stage1_Fill(void)
{
  uint8_t  ci;
  uint32_t frames;

  /* --- DMA 关（纯阻塞） --- */
  StatusBegin(1, "FILL", false);
  ci = 0; frames = 0;
  uint32_t t0 = BSP_GetTick();
  while ((BSP_GetTick() - t0) < PERF_WINDOW_MS)
  {
    ST7735_FillRectangle(0, BODY_Y, BODY_W, BODY_H, kFillPalette[ci]);
    ci = static_cast<uint8_t>((ci + 1U) % kFillPaletteLen);
    frames++;
  }
  const uint32_t cpuFps = frames;
  const uint32_t cpuMs  = (frames != 0U) ? (PERF_WINDOW_MS / frames) : 0U;

  /* --- DMA 开 --- */
  StatusBegin(1, "FILL", true);
  ci = 0; frames = 0;
  t0 = BSP_GetTick();
  while ((BSP_GetTick() - t0) < PERF_WINDOW_MS)
  {
    ST7735_FillRectangle_DMA(0, BODY_Y, BODY_W, BODY_H, kFillPalette[ci]);
    ci = static_cast<uint8_t>((ci + 1U) % kFillPaletteLen);
    frames++;
  }
  const uint32_t dmaFps = frames;
  const uint32_t dmaMs  = (frames != 0U) ? (PERF_WINDOW_MS / frames) : 0U;

  StageSummary(1, "FILL", cpuFps, cpuMs, dmaFps, dmaMs);
}

/*============================================================================
 * 环节 2/4：随机几何图形
 *  每帧重新随机一组图形，画满全屏（行渲染器），下一帧重随机 = 清屏重画。
 *  这是 CPU 密集环节 —— 每帧要做 160x60 次点测试，
 *  最能体现"计算与传输重叠"的 DMA 收益。
 *==========================================================================*/
static void Stage2_Shape(void)
{
  uint32_t frames;

  StatusBegin(2, "SHAPE", false);
  frames = 0;
  uint32_t t0 = BSP_GetTick();
  while ((BSP_GetTick() - t0) < PERF_WINDOW_MS)
  {
    ShapeGen();
    ST7735_DrawFrameRectBlocking(0, BODY_Y, BODY_W, BODY_H, ShapeRowRender);
    frames++;
  }
  const uint32_t cpuFps = frames;
  const uint32_t cpuMs  = (frames != 0U) ? (PERF_WINDOW_MS / frames) : 0U;

  StatusBegin(2, "SHAPE", true);
  frames = 0;
  t0 = BSP_GetTick();
  while ((BSP_GetTick() - t0) < PERF_WINDOW_MS)
  {
    ShapeGen();
    ST7735_DrawFrameRectEx(0, BODY_Y, BODY_W, BODY_H, ShapeRowRender, true);
    frames++;
  }
  const uint32_t dmaFps = frames;
  const uint32_t dmaMs  = (frames != 0U) ? (PERF_WINDOW_MS / frames) : 0U;

  StageSummary(2, "SHAPE", cpuFps, cpuMs, dmaFps, dmaMs);
}

/*============================================================================
 * 环节 3/4：HelloWorld + 红旗飘扬背景
 *  背景红色亮度波流动，文字 HelloWorld 永不被覆盖（字模逐点判断，画在最上层）。
 *  每帧 160x60 次像素计算 —— 计算量较大，用于观察 DMA 重叠收益。
 *==========================================================================*/
static void Stage3_Flag(void)
{
  uint32_t frames;

  StatusBegin(3, "FLAG", false);
  frames = 0;
  uint32_t t0 = BSP_GetTick();
  while ((BSP_GetTick() - t0) < PERF_WINDOW_MS)
  {
    ST7735_DrawFrameRectBlocking(0, BODY_Y, BODY_W, BODY_H, FlagRowRender);
    s_frame += 2U;
    frames++;
  }
  const uint32_t cpuFps = frames;
  const uint32_t cpuMs  = (frames != 0U) ? (PERF_WINDOW_MS / frames) : 0U;

  StatusBegin(3, "FLAG", true);
  frames = 0;
  t0 = BSP_GetTick();
  while ((BSP_GetTick() - t0) < PERF_WINDOW_MS)
  {
    ST7735_DrawFrameRectEx(0, BODY_Y, BODY_W, BODY_H, FlagRowRender, true);
    s_frame += 2U;
    frames++;
  }
  const uint32_t dmaFps = frames;
  const uint32_t dmaMs  = (frames != 0U) ? (PERF_WINDOW_MS / frames) : 0U;

  StageSummary(3, "FLAG", cpuFps, cpuMs, dmaFps, dmaMs);
}

/*============================================================================
 * 环节 4/4：总汇总
 *==========================================================================*/
static void Stage4_Summary(void)
{
  char buf[40];

  ST7735_FillScreen(ST7735::BLACK);
  ST7735_FillRectangle(0, 0, ST7735_WIDTH, BAR_H, C_BARBG);
  Text(2, 1, "[4/4] SUMMARY", ST7735::WHITE, C_BARBG);
  Text(2, 11, "FPS: CPU vs DMA", C_GRAY, C_BARBG);

  Text(4, 24, "STAGE  CPU  DMA  SPD", ST7735::WHITE, ST7735::BLACK);

  for (uint8_t i = 0; i < 3U; i++)
  {
    const uint16_t y = static_cast<uint16_t>(36U + i * 12U);
    const uint32_t sp10 = (s_cpuFps[i] != 0U) ? ((s_dmaFps[i] * 10U) / s_cpuFps[i]) : 0U;

    (void)snprintf(buf, sizeof(buf), "%-6s %3lu  %3lu  %lu.%lu",
                   s_stageName[i],
                   static_cast<unsigned long>(s_cpuFps[i]),
                   static_cast<unsigned long>(s_dmaFps[i]),
                   static_cast<unsigned long>(sp10 / 10U),
                   static_cast<unsigned long>(sp10 % 10U));
    Text(4, y, buf, (i == 0U) ? ST7735::CYAN : ((i == 1U) ? ST7735::YELLOW : ST7735::ORANGE),
         ST7735::BLACK);
  }

  Text(4, 72, "both = hardware SPI", C_GRAY, ST7735::BLACK);
  LL_mDelay(6000);
}

/* USER CODE END 4 */



/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
[[noreturn]] void Error_Handler()
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
