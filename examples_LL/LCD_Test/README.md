# LCD_Test

PY32F003 + ST7735 彩屏示例（**LL 库 + C++17 + LoveFinderLibForPY32_LL**）。
上电后在屏幕上显示三行文字：

```
exp1_LCD_Test
EcoCheatOneT1
HelloWorld!
```

> 本仓库只保留 LL 版；HAL 对照工程不在本仓库内。

## 本版本做了什么

1. **整个工程转 C++17**（与 `examples_LL/LED_Breathing` 同样的做法）
2. **屏驱换成 `LoveFinderLibForPY32_LL/ST7735`** —— 参照
   `LoveFinder830-PickSoul` 的完整 C++ 驱动（`namespace ST7735` 常量 +
   `FontDef` 字库 + 完整图形 API），底层从 STM32 HAL 移植到 PY32 LL
3. 删掉了工程内原来的简化驱动 `User/ST7735/`（`st7735.c` + `fonts.c`）

## 编译结果（Keil MDK 5.43a / Arm Compiler 6.24，`-O1` + LTO）

| 版本 | Code | RO-data | RW-data | ZI-data | Flash 合计 | RAM 合计 |
|------|------|---------|---------|---------|-----------|---------|
| HAL `LCD_Test`（简化驱动） | 8008 | 928 | 204 | 1140 | 9140 | 1344 |
| LL 旧版（简化驱动） | 4972 | 872 | 196 | 844 | 6040 | 1040 |
| LL + 完整字库 `Font_7x10` | 3496 | 2372 | 196 | 524 | 6064 | 720 |
| **LL + 字符级子集（本版本）** | **4072** | **972** | 196 | **524** | **5240** | **720** |
| 相对 HAL 差值 | −3936 | +44 | −8 | −616 | **−3900 (−42.7%)** | **−624 (−46.4%)** |

**字体是字符级编译**：本工程清单（`font_config.hpp` 的 `FONT_7X10_CHARS`）
只列了 25 个字符，所以固件里只有这 25 个字形（= 500 字节）。
库侧保存着全部 95 个字形，未引用的由编译器丢弃，完整字库（1900 字节）
完全不参与编译。字库省 1400 字节；Code 略增是因为走 `switch` 查表而非固定索引。

> 实测（重构后全量重建，`MDK-ARM/Listings/LCD_Test.map`）：
> Code=4096 / RO-data=972 / RW-data=200 / ZI-data=840，
> map 中恰好 25 个 `g_7x10_cp*` 数组，字符集
> `" !1CDEHLOTW_acdehlnoprstx"` 与清单逐一对应。

换来的是：完整图形 API（线/圆/椭圆/三角/多边形/圆角矩形）、UTF-8 中文、
字符级编译字库、图标库、`_DMA` 接口。

## 目录结构

```
LCD_Test/
├── Core/
│   ├── INC/  main.hpp gpio.hpp rcc.hpp i2c.hpp spi.hpp usart.hpp tim.hpp crc.hpp py32f003_it.hpp
│   └── SRC/  main.cpp gpio.cpp rcc.cpp i2c.cpp spi.cpp usart.cpp tim.cpp crc.cpp
│             py32f003_it.cpp（ISR，extern "C" 包裹）
│             system_py32f003.c（厂商 CMSIS，保持 C）
├── Drivers/
│   ├── CMSIS/
│   └── PY32F003_LL_Driver/
├── MDK-ARM/
│   ├── LCD_Test.uvprojx
│   └── startup_py32f003xx.s
├── LCD_Test.pysprj
└── README.md
```

### 库的切分：像素数据在库，编译清单在工程

本工程只带**一个** `font_config.hpp` —— 它只声明「要编译哪些字」：

```
LCD_Test/
└── LoveFinderLib/
    └── FontLib/
        └── font_config.hpp         ← ★ 本工程唯一的字库文件
                                        USE_FONT_7X10=1
                                        FONT_7X10_CHARS = " !1CDEHLOTW_acdehlnoprstx"（25 字符）
```

字形**像素数据**在共享库里，所有例程共用同一份：

```
LoveFinderLibForPY32_LL/FontLib/
    ├── font_manifest.json          ← 唯一真相源（字体名册 + 全部像素）
    ├── font_data.cpp               ← 全部字形的逐字符数组 + 查表（生成物）
    └── font.h                      ← 固定接口（生成物）
```

工程 include path（见 `MDK-ARM/LCD_Test.uvprojx`）：

```
../LoveFinderLib/FontLib                        ← 本工程的编译清单
../../../../LoveFinderLibForPY32_LL/FontLib     ← 共享的像素数据与接口
../../../../LoveFinderLibForPY32_LL/ST7735      ← 共享驱动
```

Keil 文件组里的 `font_data.cpp` 指向的是**库里的那一份**
（`../../../../LoveFinderLibForPY32_LL/FontLib/font_data.cpp`），
和 `LCD_DMA_Test` 是同一个文件。

> **注意**：库的 `FontLib/` 里**绝不能**出现 `font_config.hpp`。
> 库内 `#include "font_config.hpp"` 靠「库目录没有同名文件」才会经 `-I`
> 落到本工程的清单上 —— 一旦库目录里也放一份，所有工程就会共用同一份清单。

**用 PickSoul 编辑字形**：改的是库，所有引用该库的例程重新编译后一起更新。
改「本工程编译哪些字」：直接编辑 `font_config.hpp`，重新编译即可。

## 接线（与 HAL 版完全相同）

| ST7735 模块 | 开发板引脚 | 说明 |
|------------|-----------|------|
| SCL / SCK  | PA5       | SPI1_SCK（**AF0**） |
| SDA / MOSI | PA3       | SPI1_MOSI（**AF10**） |
| CS         | PA7       | LCD_CS（软件控制） |
| DC  / A0   | PA4       | LCD_DC |
| RES / RST  | PA6       | LCD_RESET |
| BL / LED   | PA12      | LCD_EN（背光） |
| VCC        | 3V3       | - |
| GND        | GND       | - |

> **PA3 与 PA5 的 AF 不同**（AF10 / AF0），写反了能编译过但屏不亮。

## 使用

1. Keil 打开 `MDK-ARM/LCD_Test.uvprojx`，编译下载；
2. 或双击仓库根目录 `expProjWrite.bat`，菜单选 `LL/LCD_Test`。

## 代码入口（`Core/SRC/main.cpp`，USER CODE 2）

```cpp
ST7735_Init();                       // 含背光使能 + 面板反色
ST7735_FillScreen(ST7735::BLACK);
ST7735_WriteString(40, 12, "exp1_LCD_Test", Font_Subset_7x10, ST7735::WHITE, ST7735::BLACK);
ST7735_WriteString(40, 32, "EcoCheatOneT1", Font_Subset_7x10, ST7735::CYAN,  ST7735::BLACK);
ST7735_WriteString(40, 52, "HelloWorld!",   Font_Subset_7x10, ST7735::RED,   ST7735::BLACK);
```

库支持的其他能力（详见 `LoveFinderLibForPY32_LL/README.md`）：

```cpp
ST7735_DrawLine / DrawCircle / FillCircle / DrawEllipse / FillEllipse
ST7735_DrawTriangle / FillTriangle / DrawPolygon / FillPolygon
ST7735_DrawRect / FillRectangle / DrawRoundRect / FillRoundRect
ST7735_WriteStringUTF8(...)          // UTF-8 中文
ST7735_Print(...)                    // printf 风格
ST7735_DrawImage / DrawIcon / SetGamma / InvertColors
```

## 屏幕参数调整

全部在 `LoveFinderLibForPY32_LL/ST7735/st7735.hpp` 的 `namespace ST7735` 里：

```cpp
constexpr uint8_t  XSTART   = 1;
constexpr uint8_t  YSTART   = 26;
constexpr uint8_t  ROTATION = (MADCTL_MX | MADCTL_MV | MADCTL_BGR);  // 0x68
constexpr bool     INVERT   = true;
```

> 这是本板面板实测可用值。参照工程 PickSoul 的另一批面板是
> `XSTART=0, YSTART=24, ROTATION=0xA8, INVERT=false`。换屏后显示错位先动这里。
>
> HAL 版工程自带的 `lcd_config_tool.py` 写死了 HAL 工程路径，
> **不能用于本版本**，请直接改上面的常量。

## 迁移中踩的坑（C++17 相关）

与 `examples_LL/LED_Breathing/README.md` 相同的三条，另加一条：

| # | 坑 | 解法 |
|---|----|----|
| 1 | 厂商 LL 头用了 C++17 已删除的 `register` | 工程 Misc Controls 加 `-Wno-register` |
| 2 | ISR 会被 C++ 名字修饰，汇编端找不到 | `py32f003_it.cpp` 里 `extern "C" { ... }` |
| 3 | Keil 需 `FileType=8`（C++）+ `<v6LangP>9</v6LangP>` | 见 `LED_Breathing/README.md` |
| 4 | **`fputc` 与 `<cstdio>` 的 `std::fputc` 冲突** | `usart.cpp` 的 printf 重定向钩子用 `extern "C"` 包裹 |

第 4 条的具体报错：

```
error: declaration conflicts with target of using declaration already in scope
  172 | int fputc(int ch, FILE *f)
```

原因：`<cstdio>` 把 `::fputc` 作为 using 声明引入了全局名字空间，
而 `fputc` 本身是 C 运行库按 C 符号名回调的重定向钩子。
用 `extern "C"` 包裹既能匹配调用约定，也避开冲突。

## 相关工程

| 工程 | 说明 |
|------|------|
| `LoveFinderLibForPY32_LL/` | 屏驱库本体（本工程引用它） |
| `examples_LL/LED_Breathing/` | 第一个 C++17 转换样板，坑的完整说明在这里 |
| `examples_LL/LCD_DMA_Test/` | DMA 加速版（SPI1_TX → DMA1_Channel1） |
| `examples_LL/Button_Test/` | 按键（EXTI）+ 屏：单击/双击/长按统计，长按阈值可调 |
| `tempLate_LL/` | LL 空工程母版（仅 `main` + 自定义库为 C++） |
