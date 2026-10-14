/**
 * @file    lf_config.h
 * @brief   工程侧 LoveFinderLib 配置 (PY32F003 + LL, LoveFinder851 示例)
 *
 * ============================================================================
 *  这个文件是什么
 * ============================================================================
 *  统一版库 (LoveFinderLibForPY32_LL) 的 ST7735 驱动 / spi_io 模块都通过
 *  `#include "lf_config.h"` 拿两样东西:
 *      ① 平台头 (SPI_TypeDef / LL_SPI_* / LL_GPIO_* / LL_DMA_*)
 *      ② 毫秒时基 lf_get_tick()
 *  以及 ST7735 的引脚 / SPI / DMA / 面板宏。
 *
 *  本文件放在工程 Core/INC (MDK include 路径的第一项), 因此优先生效。
 *  换板子 / 换屏只改这里。
 *
 *  ⚠ 与旧版的关系: 旧 st7735.hpp 把这些宏硬编码在库里; 统一后搬到工程侧,
 *    库里只剩缺省值 (st7735_unified.hpp 的 #ifndef ... #define)。
 * ============================================================================
 */
#ifndef LF_CONFIG_H
#define LF_CONFIG_H

/* 平台头: main.hpp 已 include 全部 LL 外设头 (spi/gpio/dma/system),
 * 并声明 BSP_GetTick(); gpio.hpp 提供板级引脚宏 LCD_CS_Port / LCD_CS_Pin … */
#include "main.hpp"
#include "gpio.hpp"

/*============================================================================
 * 时基 — 库 (spi_io 读超时 / ST7735 DMA 超时 / FUSB302 等) 都靠它
 *============================================================================*/
inline uint32_t lf_get_tick(void) noexcept { return BSP_GetTick(); }

/*============================================================================
 * ST7735 LCD (SPI1, 160x80)
 *
 * 面板批次: 本板实测 = BGR + RAM 偏移 1/26 + 需反色 → Panel D
 * (与旧 st7735.hpp 里硬编码的 XSTART=1 / YSTART=26 / MADCTL=0x68 / INVERT=true
 *  完全一致)。点屏异常时改成 ST7735_PANEL_A/B/C 后【重编译】逐套对比。
 *============================================================================*/
#define ST7735_PANEL_D

#define ST7735_SPI              SPI1
#define ST7735_WIDTH            160
#define ST7735_HEIGHT           80
#define ST7735_ROTATION         ST7735::DEG_0

/* 屏幕走 SPI1_TX DMA —— PY32F003 的 DMA 请求靠 SYSCFG->CFGR3 重映射,
 * 由工程 Core/SRC/spi.cpp 完成 (LL_SYSCFG_SetDMARemap_CH1(SPI1_TX))。
 * 库内 DMA 走【纯轮询】完成 (不看 NVIC), 无需开 DMA 中断。 */
#define ST7735_USE_DMA          1
#define ST7735_DMA_INSTANCE     DMA1
#define ST7735_DMA_CHANNEL      LL_DMA_CHANNEL_1
#define ST7735_DMA_TIMEOUT_MS   100

/* 控制引脚 (PIN 为 LL_GPIO_PIN_x) */
#define ST7735_CS_PORT          LCD_CS_Port
#define ST7735_CS_PIN           LCD_CS_Pin
#define ST7735_DC_PORT          LCD_DC_Port
#define ST7735_DC_PIN           LCD_DC_Pin
#define ST7735_RES_PORT         LCD_RESET_Port
#define ST7735_RES_PIN          LCD_RESET_Pin
#define ST7735_EN_PORT          LCD_EN_Port      /* 背光/电源使能 */
#define ST7735_EN_PIN           LCD_EN_Pin

#endif /* LF_CONFIG_H */
