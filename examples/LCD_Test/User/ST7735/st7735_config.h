/**
 * @file st7735_config.h
 * @brief ST7735 快速配置入口（供 lcd_config_tool.py 修改）
 *
 * 本文件的宏对应面板的几何 / 旋转 / 颜色序 / 偏移 / 反色参数。
 * 运行工程根目录下的 lcd_config_tool.py 即可快速修改，无需手改驱动。
 *
 * 预设参考（与 LoveFinder491_PowerOneT2 的 ST7735_PanelConfig 一致）：
 *   Panel A：原批次，BGR，偏移 (0,24)，无反色，默认 DEG_0  横屏 160x80
 *   Panel B：新批次，RGB，偏移 (1,26)，需反色，默认 DEG_180 横屏 160x80
 *   Panel C：新批次镜像，BGR，偏移 (1,26)，需反色，默认 DEG_0 横屏 160x80
 */

#ifndef ST7735_CONFIG_H
#define ST7735_CONFIG_H

/* 逻辑分辨率（按当前旋转后的可视区域） */
#define ST7735_WIDTH      160
#define ST7735_HEIGHT     80

/* MADCTL（0x36 参数）：旋转 / 镜像 / 颜色序。
 * 位定义：MY=0x80 MX=0x40 MV=0x20 ML=0x10 RGB=0x00 BGR=0x08 MH=0x04
 * Panel A DEG_0 横屏：MX|MV|BGR = 0x68 */
#define ST7735_MADCTL     0x68

/* 面板列/行起始偏移（CASET/RASET 加到逻辑坐标上） */
#define ST7735_X_OFFSET   1
#define ST7735_Y_OFFSET   26

/* 是否反色：1 = 启动时发 INVON，0 = INVOFF */
#define ST7735_INVERT     1

#endif /* ST7735_CONFIG_H */
