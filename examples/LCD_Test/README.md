# LCD_Test

PY32F003 + ST7735 彩屏示例工程：上电后屏幕显示两行文字：

```
Hello
EcoCheatOneT1
```

## 说明

- 主控：PY32F003F18U6-E（与 LoveFinder491_PowerOneT2 工程同一块开发板）
- 屏幕：ST7735 控制器 0.96 英寸 160x80（与 POT2 工程同款屏）
- 本工程由 PY32F003 基础工程（LoveFinder851-EcoCheatOneT1）复制而来，
  保留了 PyStudio 生成的引脚定义与 HAL 代码，仅新增 ST7735 驱动。

## 目录结构

```
LCD_Test/
|-- Core/                  # PyStudio 生成的 HAL 配置代码
|-- Drivers/               # PY32F003 HAL 驱动 / CMSIS
|-- MDK-ARM/               # Keil MDK-ARM 工程（LCD_Test.uvprojx）
|-- User/ST7735/           # ST7735 驱动 + 5x7 字体（纯 C，阻塞 SPI）
|   |-- st7735.c / st7735.h
|   `-- fonts.c / fonts.h
`-- LCD_Test.pysprj       # PyStudio 工程文件（含引脚定义）
```

## 接线（LCD -> 开发板）

| ST7735 模块 | 开发板引脚 | 说明 |
|------------|-----------|------|
| SCL / SCK  | PA5       | SPI1_SCK |
| SDA / MOSI | PA3       | SPI1_MOSI |
| CS         | PA7       | LCD_CS（软件控制） |
| DC  / A0   | PA4       | LCD_DC |
| RES / RST  | PA6       | LCD_RESET |
| BL / LED   | PA12      | LCD_EN（背光） |
| VCC        | 3V3       | - |
| GND        | GND       | - |

## 使用

1. 用 Keil MDK 打开 `MDK-ARM/LCD_Test.uvprojx`，编译并下载；
2. 或使用 PyStudio 打开 `LCD_Test.pysprj` 后生成/编译。


## 快速配置工具 lcd_config_tool.py

工程根目录提供 `lcd_config_tool.py`，可直接修改屏幕参数与演示文本，无需手改源码：

```bash
python lcd_config_tool.py                      # 交互菜单
python lcd_config_tool.py get                  # 查看当前配置
python lcd_config_tool.py preset A|B|C         # 切换面板预设（A/B/C 批次）
python lcd_config_tool.py rotation 0|90|180|270# 切换旋转方向
python lcd_config_tool.py offset X Y           # 设置面板偏移
python lcd_config_tool.py invert 0|1           # 设置反色
python lcd_config_tool.py size W H             # 设置逻辑分辨率
python lcd_config_tool.py text <1|2> <内容> [x] [y] [前景色] [背景色]  # 改演示文本
```

面板预设对应 POT2 工程的 ST7735_PanelConfig：

| 预设 | 说明 |
|------|------|
| A | 原批次：BGR，偏移 0/24，无反色，默认 DEG_0（160x80） |
| B | 新批次：RGB，偏移 1/26，需反色，默认 DEG_180（160x80） |
| C | 新批次镜像：BGR，偏移 1/26，需反色，默认 DEG_0（160x80） |

工具修改的文件：
- `User/ST7735/st7735_config.h`（几何 / MADCTL / 偏移 / 反色）
- `Core/SRC/main.c`（两行 ST7735_DrawString 演示文本）

## 代码入口

`Core/SRC/main.c` 中 `USER CODE 2`：

```c
ST7735_Init();
ST7735_FillScreen(ST7735_BLACK);
ST7735_DrawString(40, 18, "Hello", ST7735_WHITE, ST7735_BLACK);
ST7735_DrawString(8, 34, "EcoCheatOneT1", ST7735_CYAN, ST7735_BLACK);
```

屏幕方向 / 偏移 / 反色参数统一在 `User/ST7735/st7735_config.h`，用 `lcd_config_tool.py` 修改即可。