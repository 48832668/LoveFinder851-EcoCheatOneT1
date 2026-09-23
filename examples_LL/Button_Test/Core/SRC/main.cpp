/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    main.cpp
  * @brief   Button_Test —— 按键单击/双击/长按统计 (ST7735 屏幕显示)
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
#include "st7735.hpp"   /* LoveFinderLibForPY32_LL/ST7735 */
#include "BUTTON.hpp"   /* LoveFinderLibForPY32_LL/BUTTON */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* 双击是否顺带切换「长按阈值」档位 —— 用来演示运行期 setLongPressMs()。
   置 0 则双击只计数、不改阈值（纯统计）。 */
#ifndef BUTTON_TEST_DOUBLECLICK_CYCLES_THRESHOLD
#define BUTTON_TEST_DOUBLECLICK_CYCLES_THRESHOLD   1
#endif

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* 1 ms tick counter, incremented from SysTick_Handler() */
volatile uint32_t uwTick = 0;
/* USER CODE BEGIN PV */

namespace {

/*============================================================================
 * 按键对象
 *
 * 引脚 = 板级 KEY_INT（PA1，见 Core/INC/gpio.hpp）。
 * 硬件上是 3V3 上拉 + 按键到地 -> 按下为低电平 -> activeLow = true（默认）。
 * BUTTON 库的 init() 会自动把该引脚配成输入+上拉、EXTI 下降沿，并使能 NVIC。
 *============================================================================*/
LoveFinderLib::Button g_button;

/*============================================================================
 * 长按阈值档位 —— 双击切换一档，演示「长按阈值运行期可调」
 *
 * 除了这里的运行期切换，阈值还有另外两个可调入口：
 *   1) 编译期: 在包含 BUTTON.hpp 之前 #define BUTTON_LONG_PRESS_MS_DEFAULT
 *   2) 初始化: BUTTON_Config cfg; cfg.longPressMs = 800; g_button.init(cfg);
 *   3) 运行期: g_button.setLongPressMs(ms);
 *============================================================================*/
constexpr uint16_t kLongPressPresets[] = { 300u, 500u, 1000u, 1500u, 2000u };
constexpr uint8_t  kLongPressPresetCount =
    static_cast<uint8_t>(sizeof(kLongPressPresets) / sizeof(kLongPressPresets[0]));
uint8_t g_presetIndex = 2u;      /* 上电默认 1000 ms */

/*============================================================================
 * 屏幕布局（160x80 横屏，Font_7x10 -> 字宽 7px、行高 10px）
 *
 *   y=0   Button_Test
 *   y=11  CLICK :    0
 *   y=22  DOUBLE:    0
 *   y=33  LONG  :    0
 *   y=44  LP:1000ms DC:300ms
 *   y=55  CHG:  0%   2x:LP+
 *   y=66  [====================]   <- 长按充能进度条 (按下时边框变红)
 *============================================================================*/
constexpr uint16_t ROW_TITLE  = 0;
constexpr uint16_t ROW_CLICK  = 11;
constexpr uint16_t ROW_DOUBLE = 22;
constexpr uint16_t ROW_LONG   = 33;
constexpr uint16_t ROW_THRESH = 44;
constexpr uint16_t ROW_CHG    = 55;

constexpr uint16_t HINT_X     = 70;   /* "2x:LP+" 的 x */

constexpr uint16_t BAR_X = 2;
constexpr uint16_t BAR_Y = 66;
constexpr uint16_t BAR_W = 156;
constexpr uint16_t BAR_H = 8;
constexpr uint16_t BAR_INNER_W = BAR_W - 2u;
constexpr uint16_t BAR_INNER_H = BAR_H - 2u;

/*============================================================================
 * UI 缓存 —— 只在数值真的变化时才重绘那一行
 *
 * 屏幕是阻塞 SPI（4 MHz），一次 13 字符的行 ≈ 3.6 ms。全屏每帧重绘会让
 * 主循环变慢，所以这里按「字段变化」增量刷新。
 *============================================================================*/
struct UiCache {
    uint32_t click;
    uint32_t dbl;
    uint32_t lng;
    uint16_t lp;
    uint16_t dc;
    uint8_t  chg;
    uint16_t fillW;      /* 进度条已填充像素宽度 */
    bool     pressed;
};

UiCache g_ui = {
    0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,   /* 哨兵: 首次必定重绘 */
    0xFFFFu, 0xFFFFu, 0xFFu, 0xFFFFu,
    false
};

/*--------------------------------------------------------------------------
 * 把无符号整数右对齐写进固定宽度字段（空格补齐，不写结束符）
 *------------------------------------------------------------------------*/
void format_u32(char* dst, uint32_t value, uint8_t width)
{
    for (uint8_t i = 0; i < width; i++)
    {
        dst[i] = ' ';
    }
    uint8_t pos = width;
    do
    {
        if (pos == 0u) { break; }        /* 位数超宽 -> 截断高位 */
        pos--;
        dst[pos] = static_cast<char>('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u);
}

/*--------------------------------------------------------------------------
 * 写一行 "标签 + 定宽数值"（bgcolor 用黑色，天然擦掉旧值）
 *------------------------------------------------------------------------*/
void ui_write_field(uint16_t x, uint16_t y, const char* label,
                    uint32_t value, uint8_t width, uint16_t color)
{
    char line[32];
    uint8_t n = 0;

    while ((label[n] != '\0') && (n < 24u))
    {
        line[n] = label[n];
        n++;
    }
    format_u32(&line[n], value, width);
    n = static_cast<uint8_t>(n + width);
    line[n] = '\0';

    ST7735_WriteString(x, y, line, Font_7x10, color, ST7735::BLACK);
}

/*--------------------------------------------------------------------------
 * 阈值行: "LP:1000ms DC:300ms"
 *------------------------------------------------------------------------*/
void ui_write_thresholds(uint16_t lp, uint16_t dc)
{
    char line[24];
    uint8_t n = 0;

    const char* a = "LP:";
    while (*a != '\0') { line[n++] = *a++; }
    format_u32(&line[n], lp, 4u); n = static_cast<uint8_t>(n + 4u);

    const char* b = "ms DC:";
    while (*b != '\0') { line[n++] = *b++; }
    format_u32(&line[n], dc, 4u); n = static_cast<uint8_t>(n + 4u);

    line[n++] = 'm';
    line[n++] = 's';
    line[n] = '\0';

    ST7735_WriteString(0, ROW_THRESH, line, Font_7x10, ST7735::GREEN, ST7735::BLACK);
}

/*--------------------------------------------------------------------------
 * 充能行: "CHG:  0%"（定宽 8 字符）
 *------------------------------------------------------------------------*/
void ui_write_charge(uint8_t pct)
{
    char line[12];
    uint8_t n = 0;

    const char* a = "CHG:";
    while (*a != '\0') { line[n++] = *a++; }
    format_u32(&line[n], pct, 3u); n = static_cast<uint8_t>(n + 3u);
    line[n++] = '%';
    line[n] = '\0';

    ST7735_WriteString(0, ROW_CHG, line, Font_7x10, ST7735::WHITE, ST7735::BLACK);
}

/*--------------------------------------------------------------------------
 * 只在屏幕上画一次的静态内容
 *------------------------------------------------------------------------*/
void ui_draw_static()
{
    ST7735_WriteString(0, ROW_TITLE, "Button_Test", Font_7x10, ST7735::WHITE, ST7735::BLACK);

    /* 双击手势提示 */
    ST7735_WriteString(HINT_X, ROW_CHG, "2x:LP+", Font_7x10, ST7735::ORANGE, ST7735::BLACK);

    /* 充能进度条外框 */
    ST7735_DrawRect(BAR_X, BAR_Y, BAR_W, BAR_H, ST7735::WHITE);
}

/*--------------------------------------------------------------------------
 * 充能进度条：按像素量化，只有填充宽度真的变了才重绘
 *------------------------------------------------------------------------*/
void ui_update_bar(uint8_t pct, bool pressed)
{
    if (pressed != g_ui.pressed)
    {
        ST7735_DrawRect(BAR_X, BAR_Y, BAR_W, BAR_H,
                        pressed ? ST7735::RED : ST7735::WHITE);
        g_ui.pressed = pressed;
    }

    const uint16_t fillW =
        static_cast<uint16_t>((static_cast<uint32_t>(BAR_INNER_W) * pct) / 100u);

    if (fillW == g_ui.fillW)
    {
        return;
    }
    g_ui.fillW = fillW;

    if (fillW > 0u)
    {
        ST7735_FillRectangle(static_cast<uint16_t>(BAR_X + 1u), static_cast<uint16_t>(BAR_Y + 1u),
                             fillW, BAR_INNER_H, ST7735::ORANGE);
    }
    if (fillW < BAR_INNER_W)
    {
        ST7735_FillRectangle(static_cast<uint16_t>(BAR_X + 1u + fillW), static_cast<uint16_t>(BAR_Y + 1u),
                             static_cast<uint16_t>(BAR_INNER_W - fillW), BAR_INNER_H,
                             ST7735::BLACK);
    }
}

/*--------------------------------------------------------------------------
 * 增量刷新整屏
 *------------------------------------------------------------------------*/
void ui_update()
{
    const uint32_t click = g_button.getClickCount();
    const uint32_t dbl   = g_button.getDoubleClickCount();
    const uint32_t lng   = g_button.getLongPressCount();
    const uint16_t lp    = g_button.getLongPressMs();
    const uint16_t dc    = g_button.getDoubleClickMs();
    const uint8_t  chg   = g_button.getChargePercent();
    const bool     down  = g_button.isPressed();

    if (click != g_ui.click)
    {
        ui_write_field(0, ROW_CLICK, "CLICK :", click, 5u, ST7735::CYAN);
        g_ui.click = click;
    }
    if (dbl != g_ui.dbl)
    {
        ui_write_field(0, ROW_DOUBLE, "DOUBLE:", dbl, 5u, ST7735::MAGENTA);
        g_ui.dbl = dbl;
    }
    if (lng != g_ui.lng)
    {
        ui_write_field(0, ROW_LONG, "LONG  :", lng, 5u, ST7735::YELLOW);
        g_ui.lng = lng;
    }
    if ((lp != g_ui.lp) || (dc != g_ui.dc))
    {
        ui_write_thresholds(lp, dc);
        g_ui.lp = lp;
        g_ui.dc = dc;
    }
    if (chg != g_ui.chg)
    {
        ui_write_charge(chg);
        g_ui.chg = chg;
    }

    ui_update_bar(chg, down);
}

} // namespace

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

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
     （与 LCD_Test 的外设集一致：GPIO / I2C1 / SPI1 / USART1 / TIM3 / CRC） */
  Studio_GPIO_Init();
  Studio_I2C1_Init();
  Studio_SPI1_Init();
  Studio_USART1_Init();
  Studio_TIM3_Init();
  Studio_CRC_Init();
  /* USER CODE BEGIN 2 */

  /* ---- 屏幕 ---- */
  ST7735_Init();                       /* 含背光使能 + 面板反色 */
  ST7735_FillScreen(ST7735::BLACK);
  ui_draw_static();

  /* ---- 按键 ----
     板级默认引脚 KEY_INT(PA1) + 默认配置:
         activeLow     = true    (3V3 上拉，按下为低)
         debounceMs    = 20
         longPressMs   = 1000    (可调 —— 见 kLongPressPresets)
         doubleClickMs = 300
         chargeFullMs  = 2000    (长按后继续按住，每填满一次长按次数 +1)
         decayMs       = 3000    (没填满就松开，进度慢慢消退)
     init() 内部会: 配置 GPIO 输入+上拉、EXTI 下降沿、使能 NVIC，
                    并把实例登记到按键表供 EXTI 中断分发。 */
  g_button.init();

  ui_update();                         /* 首次全量刷新 (缓存是哨兵值) */
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */
    /* USER CODE BEGIN 3 */
    /* 按键状态机: 必须 1-10ms 调一次 (双击窗口 300ms 依赖它计时) */
    const LoveFinderLib::e_BUTTON_Event evt = g_button.update();

    if (evt == LoveFinderLib::e_BUTTON_Event::DOUBLE_CLICK)
    {
#if BUTTON_TEST_DOUBLECLICK_CYCLES_THRESHOLD
      /* 双击 -> 切到下一个长按阈值档位 (运行期改阈值, 无需重新编译) */
      g_presetIndex = static_cast<uint8_t>((g_presetIndex + 1u) % kLongPressPresetCount);
      g_button.setLongPressMs(kLongPressPresets[g_presetIndex]);
#endif
    }

    /* 屏幕增量刷新 (只在数值变化时写 SPI) */
    ui_update();

    LL_mDelay(2);
    /* USER CODE END 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief  Return the 1 ms tick counter maintained by SysTick_Handler().
  * @note   LL_mDelay() only polls the SysTick COUNTFLAG and keeps no counter,
  *         so this is the millisecond time base for application code.
  *         LoveFinderLibForPY32_LL/BUTTON 的按键状态机也用它计时。
  * @retval Milliseconds since reset
  */
uint32_t BSP_GetTick() noexcept
{
  return uwTick;
}

/* USER CODE BEGIN 4 */

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
