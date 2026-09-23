# Button_Test

PY32F003 + **按键（EXTI）+ ST7735 彩屏**示例
（**LL 库 + C++17 + LoveFinderLibForPY32_LL**）。

按键在硬件上是 **3V3 上拉 + 按键到地**，所以 **下降沿 = 按下**。
上电后屏幕实时显示按键的 **单击 / 双击 / 长按** 统计，以及 **长按阈值**（可调）、
**双击间隔阈值** 和 **长按充能进度**：

```
Button_Test
CLICK :    0
DOUBLE:    0
LONG  :    0
LP:1000ms DC:300ms
CHG:  0%   2x:LP+
[====================]      <- 长按充能进度条（按住时外框变红）
```

> 本工程由 `tempLate_LL` 空工程母版复制而来，并按 `examples_LL/LCD_Test` 的
> 既有做法转成 C++17、接入共享库 `LoveFinderLibForPY32_LL`
> （ST7735 屏驱 + FontLib 字库 + **新增的 BUTTON 按键库**）。

## 交互说明

| 操作 | 屏幕上的变化 | 说明 |
|------|-------------|------|
| **单击** | `CLICK` +1 | 释放后等满双击窗口（300 ms）才确认，避免和双击混淆 |
| **双击** | `DOUBLE` +1，并且 `LP:` 切到下一档 | 双击同时演示「长按阈值运行期可调」（见下） |
| **长按** ≥ `LP` | `LONG` +1 | 达到阈值瞬间触发一次，屏幕 `CHG` 开始充能 |
| 长按后**继续按住** | `CHG` 涨到 100% → `LONG` 再 +1，然后从 0 继续 | 「长按充能」：每填满一次算一次长按 |
| 长按**没填满就松开** | `CHG` 慢慢退回 0 | 消退时间 = `decayMs`（默认 3000 ms） |
| 按住期间 | 进度条外框由白变**红** | 直观看到「当前是按下状态」 |

**长按阈值档位**（双击循环切换）：
`300 → 500 → 1000 → 1500 → 2000 → 300 …` ms，上电默认 **1000 ms**。

> 不想要「双击改阈值」这个演示行为，把 `Core/SRC/main.cpp` 里的
> `BUTTON_TEST_DOUBLECLICK_CYCLES_THRESHOLD` 置 `0` 即可 —— 双击就只计数。

## 本版本做了什么

1. **从 `tempLate_LL` 复制出本工程**，并按例程惯例转 **C++17**
   （外设 `Core/{INC,SRC}/*` 由 `.c/.h` 改为 `.cpp/.hpp`，ISR 用 `extern "C"` 包裹）
2. **新增按键库 `LoveFinderLibForPY32_LL/BUTTON/`**
   —— 从 `LoveFinderLibForSTM32_HAL/BUTTON` 移植到 PY32 LL：
   - `HAL_GPIO_Init` + `GPIO_MODE_IT_FALLING` → `LL_GPIO_Init` + `LL_EXTI_SetEXTISource` + `LL_EXTI_Init`
   - `HAL_GetTick()` → `BSP_GetTick()`
   - `HAL_GPIO_EXTI_Callback(pin)` → `Button::dispatchExti(extiLine)`（库内自带实例表，
     工程的 EXTI 中断只需一行）
   - API 与状态机与 STM32 版**完全一致**，可直接替换
3. **屏驱沿用共享库** `LoveFinderLibForPY32_LL/ST7735` + 字符级编译字库
4. **按键 GPIO 用正确极性**：PA1 = 输入 + 内部上拉兜底、EXTI **下降沿**
   （`tempLate_LL` 的 `gpio.c` 本来就是下降沿，但 `LCD_Test` 里被改成了上升沿，
   本例程按「3V3 上拉」的要求用下降沿）
5. **屏幕按「字段变化」增量刷新** —— 阻塞 SPI 下全屏重绘会拖慢主循环，
   所以每行只在数值真的变了才重写

## 编译结果（Keil MDK 5.43a / Arm Compiler 6.24，`-O1` + LTO）

| 工程 | Code | RO-data | RW-data | ZI-data | Flash 合计 | RAM 合计 |
|------|------|---------|---------|---------|-----------|---------|
| `LCD_Test`（仅屏） | 4096 | 972 | 200 | 840 | 5268 | 1040 |
| **`Button_Test`（本工程）** | **5940** | **1640** | **228** | **1444** | **7808** | **1672** |
| 差值 | +1844 | +668 | +28 | +604 | +2540 | +632 |

- **RO +668**：本工程字库编译 **38 个字符**（760 B 字形），`LCD_Test` 只有 25 个（500 B）；
  其余是 `"Button_Test"` / `"CLICK :"` / `"LP:...ms DC:...ms"` 这些字面量。
- **ZI +604**：`startup_py32f003xx.s` 的栈保留了模板的 **0x400（1 KB）**，
  而 `LCD_Test` 用的是 0x200（0.5 KB）。本工程多一层 `ui_write_field()` 调用，
  栈留宽一点更稳。
  （HEAP 段无人引用，被链接器直接丢弃，不占 RAM。）

```
Program Size: Code=5940 RO-data=1640 RW-data=228 ZI-data=1444
0 Error(s), 0 Warning(s)
```

> MicroLIB 的 "does not support C++" 是厂商默认配置，可忽略（不引用 libc++）。
> 实测 `Button_Test.map` 里恰好 **38 个** `g_7x10_cp*` 字形数组，
> 与 `font_config.hpp` 的清单逐一对应（38 × 10 行 × 2 B = 760 B）。

## 目录结构

```
Button_Test/
├── Core/
│   ├── INC/  main.hpp gpio.hpp rcc.hpp i2c.hpp spi.hpp usart.hpp tim.hpp crc.hpp py32f003_it.hpp
│   └── SRC/  main.cpp gpio.cpp rcc.cpp i2c.cpp spi.cpp usart.cpp tim.cpp crc.cpp
│             py32f003_it.cpp（ISR，extern "C" 包裹；EXTI1 分发到按键库）
│             system_py32f003.c（厂商 CMSIS，保持 C）
├── Drivers/
│   ├── CMSIS/
│   └── PY32F003_LL_Driver/
├── LoveFinderLib/
│   └── FontLib/
│       └── font_config.hpp      ← 本工程唯一的字库文件（38 字符清单）
├── MDK-ARM/
│   ├── Button_Test.uvprojx
│   └── startup_py32f003xx.s
└── README.md
```

> 例程目录里**不带 `.pysprj`** —— 例程是手工维护的 Keil 工程，
> 只有 `tempLate_LL/` 母版保留 PyStudio 工程文件。见下文「关于工程名」。

工程 include path（见 `MDK-ARM/Button_Test.uvprojx`）：

```
../Core/INC
../LoveFinderLib/FontLib                        ← 本工程的编译清单
../../../../LoveFinderLibForPY32_LL/FontLib     ← 共享的字形像素与接口
../../../../LoveFinderLibForPY32_LL/ST7735      ← 共享屏驱
../../../../LoveFinderLibForPY32_LL/BUTTON      ← 共享按键库
../Drivers/CMSIS/Include
../Drivers/PY32F003_LL_Driver/Inc
../Drivers/CMSIS/Device/PY32F003/Include
```

Keil 文件组里新增一组 `LoveFinderLibForPY32_LL/BUTTON`，里面只有
`../../../../LoveFinderLibForPY32_LL/BUTTON/BUTTON.cpp`（`<FileType>8</FileType>`）。

> 字库的架构（像素在库、清单在工程）见
> `LoveFinderLibForPY32_LL/README.md`；本工程只带一份 `font_config.hpp`。

## 接线

### 按键（板载，无需外接）

| 信号 | 开发板引脚 | 说明 |
|------|-----------|------|
| KEY_INT | **PA1** | 3V3 上拉 + 按键到地 → **下降沿 = 按下**；EXTI1 |

> 库内还会再开 `LL_GPIO_PULL_UP` 内部上拉兜底，防止引脚悬空误触发。

### ST7735 彩屏（与 `LCD_Test` 完全相同）

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

1. Keil 打开 `MDK-ARM/Button_Test.uvprojx`，编译下载；
2. 或双击仓库根目录 `expProjWrite.bat`，菜单选 `Button_Test`。

## 代码入口

### 按键初始化（`Core/SRC/main.cpp`，USER CODE 2）

```cpp
/* 板级默认引脚 KEY_INT(PA1) + 默认配置 */
g_button.init();
```

`init()` 内部做了三件事（见 `BUTTON/BUTTON.hpp` 的 `configureExti()`）：

1. GPIO 配成**输入 + 内部上拉**（外部已有 3V3 上拉，这里兜底）
2. **EXTI 下降沿** + `LL_EXTI_SetEXTISource(PORTA, LINE1)`
3. 使能 NVIC（`EXTI0_1_IRQn`，优先级 `BUTTON_IRQ_PRIORITY` = 2）
   并把实例登记到库内按键表

### EXTI 中断分发（`Core/SRC/py32f003_it.cpp`）

```cpp
void EXTI0_1_IRQHandler(void)
{
  if (LL_EXTI_IsActiveFlag(LL_EXTI_LINE_0) != 0U) { LL_EXTI_ClearFlag(LL_EXTI_LINE_0); /* PA0 FUSB_INT */ }
  if (LL_EXTI_IsActiveFlag(LL_EXTI_LINE_1) != 0U)
  {
    /* dispatchExti() 内部会清挂起标志，再分发给所有 EXTI 线匹配的按键实例 */
    (void)LoveFinderLib::Button::dispatchExti(LL_EXTI_LINE_1);
  }
}
```

> **必须**有这一步，否则按下的下降沿不会被记录，计数永远是 0。

### 主循环

```cpp
while (1)
{
  const LoveFinderLib::e_BUTTON_Event evt = g_button.update();   // 1-10ms 一次
  if (evt == LoveFinderLib::e_BUTTON_Event::DOUBLE_CLICK) { /* 切长按阈值档位 */ }
  ui_update();                                                    // 屏幕增量刷新
  LL_mDelay(2);
}
```

`update()` 负责消抖、双击窗口计时、长按阈值判定和充能/消退，
**调用间隔必须 ≤ 10 ms**（双击窗口 300 ms 靠它计时；间隔过大单击会被误判）。

## 长按阈值怎么调（三个入口）

| 入口 | 写法 | 适用场景 |
|------|------|---------|
| **编译期** | 在包含 `BUTTON.hpp` 之前 `#define BUTTON_LONG_PRESS_MS_DEFAULT 1500` | 固定阈值，零运行开销 |
| **初始化** | `BUTTON_Config cfg = BUTTON_Config::getDefault(); cfg.longPressMs = 800; g_button.init(cfg);` | 上电时按条件定 |
| **运行期** | `g_button.setLongPressMs(1500);` / `g_button.getLongPressMs()` | 运行中随时改（本工程用双击演示） |

本工程三种都用到了：默认值走宏（`BUTTON_LONG_PRESS_MS_DEFAULT` = 1000），
`init()` 用默认配置，**双击**时调用 `setLongPressMs()` 切档位 —— 屏幕上
`LP:` 的值会立刻跟着变，是「阈值真的改了」的直接证据。

> 阈值传 0 会被钳到 1（避免除零）。双击间隔同理：`setDoubleClickMs()`。

## 按键行为细节（`BUTTON` 库）

| 参数 | 默认值 | 宏 / 设置方法 |
|------|-------|--------------|
| 消抖时间 | 20 ms | `BUTTON_DEBOUNCE_MS_DEFAULT` |
| 长按阈值 | 1000 ms | `BUTTON_LONG_PRESS_MS_DEFAULT` / `setLongPressMs()` |
| 双击间隔 | 300 ms | `BUTTON_DOUBLE_CLICK_MS_DEFAULT` / `setDoubleClickMs()` |
| 充能填满时间 | 2000 ms | `BUTTON_CHARGE_FULL_MS_DEFAULT` |
| 消退时间 | 3000 ms | `BUTTON_DECAY_MS_DEFAULT` |
| 低电平有效 | true | `BUTTON_ACTIVE_LOW_DEFAULT` / `cfg.activeLow` |

- **单击**：按下→释放→等满 `doubleClickMs` 没有第二次按下，才计一次单击。
- **双击**：两次短按间隔 < `doubleClickMs`。
- **长按**：按住时间 ≥ `longPressMs` 的瞬间触发一次（不是松手才触发）。
- **充能**：长按触发后继续按住，每满 `chargeFullMs` → `LONG` 再 +1（可连续累加）。
- 事件计数用 `getClickCount()` / `getDoubleClickCount()` / `getLongPressCount()` 读；
  另有事件标志位 `peekFlags()` / `testFlag()` / `getAndClearFlags()` 供 UI 轮询。

详见 `LoveFinderLibForPY32_LL/BUTTON/README.md`。

## 与 `LCD_Test` / `tempLate_LL` 的关系与差异

| 项 | `tempLate_LL` | `LCD_Test` | **`Button_Test`** |
|----|--------------|-----------|------------------|
| 语言 | 只有 `main` 是 C++ | 全 C++17 | 全 C++17 |
| 屏驱 | 无 | ST7735 | ST7735 |
| 按键 | 无 | 无 | **BUTTON 库（EXTI1 / PA1）** |
| 字库清单 | 无 | 25 字符 | **38 字符** |
| 栈 | 0x400 | 0x200 | **0x400**（多一层调用，留宽） |
| 堆 | 0x400 | 0x100 | 0x100 |
| 外设集 | GPIO/I2C1/SPI1/USART1/TIM3/CRC | 同左 | 同左 |

### 关于 `Drivers/` 的目录名（不是 HAL/LL 混用）

**所有工程（模板 + 例程）的编译配置都是纯 LL**，实测：

- `<Define>PY32F003x8,USE_FULL_LL_DRIVER</Define>`
- Keil 文件组里只有 `py32f0xx_ll_*.c`，**HAL 源一个都没参与编译**（0 个）

目录名的差异只是「厂商驱动包放了哪一份」：

| 目录 | 内容 | 出现在 |
|------|------|--------|
| `PY32F003_HAL_Driver/` | Puya 导出包的**原样**目录 —— 名字叫 HAL，但 `Inc/` `Src/` 里 HAL 与 LL 两套源码都在（28 个 `hal.h` + 22 个 `ll.h`） | `tempLate_LL`、`LCD_DMA_Test`（历史遗留） |
| `PY32F003_LL_Driver/` | 裁剪过的**纯 LL 副本**（22 个 `ll.h` / 16 个 `ll.c`，0 个 HAL 文件） | `LCD_Test`、`LED_Breathing`、**本工程** |

> 所以「`PY32F003_HAL_Driver` 目录里有 HAL 文件」只是厂商包的组织方式，
> **不代表工程在用 HAL**。本工程沿用例程那份纯 LL 副本，只是目录更干净、体积更小。

### 关于工程名

例程目录**不带 `.pysprj`** —— 只有 `tempLate_LL/` 母版保留 PyStudio 工程文件。
所以本工程是**纯手工维护的 Keil 工程**，工程名直接写在 `uvprojx` 里：

```xml
<TargetName>Button_Test</TargetName>
<OutputName>Button_Test</OutputName>
```

> 母版的 `.pysprj` 里 `"name"` 是 `EmptyProj_LL`（PyStudio 界面上改不了），
> 所以母版的 Keil 工程一直叫 `EmptyProj_LL.uvprojx` —— 例程要自己的名字，
> 把 `uvprojx` 里上面两处改掉、文件也改名即可（本工程就是这么来的）。

> `tools/expProjWrite.ps1` 的菜单**用的是目录名**（`$projDir.Name`），
> 不依赖 `.uvprojx` 的文件名 —— 工程文件叫什么都能被扫到。

> ⚠️ 如果哪天把本工程重新导入 PyStudio 并导出，会**再生成一份 C 版
> `main.c`/`main.h`**，和 `main.cpp`/`main.hpp` 冲突（两个 `main()`），
> 并把外设头的 `#include "main.hpp"` 改写成 `"main.h"`。导出后请照
> `tempLate_LL/README.md` §2 的 6 条清单逐条补回。

## 踩的坑

与 `LCD_Test` 相同的四条（`register` / ISR 名字修饰 / Keil C++ 设置 / `fputc` 冲突）
见 `examples_LL/LED_Breathing/README.md` 与 `LCD_Test/README.md`。本工程另加：

| # | 坑 | 解法 |
|---|----|----|
| 1 | **EXTI 挂起标志必须手动清**，否则中断会反复进入 | 库的 `dispatchExti()` 内部 `LL_EXTI_ClearFlag()`；工程侧不要重复清 |
| 2 | 厂商 `py32f0xx_ll_exti.h` 的 `LL_EXTI_CONFIG_LINE5` 起掩码只有 **1 位**（选不了 PORTF），与 `LINE0..4` 的 3 位不一致 | `BUTTON.cpp` 不依赖这些宏，自己按 `EXTICR` 的 8 位字段编码（`extiConfigLine()`） |
| 3 | **极性**：`LCD_Test` 里 PA1 的 EXTI 是**上升沿**（那是别的用途） | 本例程按「3V3 上拉、按下为低」改成**下降沿** —— 沿选错了按下去毫无反应 |
| 4 | **`BSP_GetTick()` 必须真的在跑**（由 `SysTick_Handler()` 自增） | 若 SysTick 中断没使能（`LL_InitTick()` 漏置 `TICKINT`，见 `tempLate_LL/README.md` 补丁 1），按键状态机会完全不动 |

## 相关工程 / 库

| 对象 | 说明 |
|------|------|
| `LoveFinderLibForPY32_LL/BUTTON/` | 本工程新增的按键库（EXTI + 状态机 + 充能） |
| `LoveFinderLibForPY32_LL/ST7735/` | 屏驱库本体 |
| `LoveFinderLibForPY32_LL/FontLib/` | 共享字形像素（本工程只声明编译哪些字） |
| `LoveFinderLibForSTM32_HAL/BUTTON/` | 按键库的 STM32 HAL 原版（移植来源） |
| `examples_LL/LCD_Test/` | 只带屏的例程，本工程的屏幕部分与它一致 |
| `examples_LL/LCD_DMA_Test/` | DMA 加速版屏显示例程 |
| `examples_LL/LED_Breathing/` | 第一个 C++17 转换样板，坑的完整说明 |
| `tempLate_LL/` | LL 空工程母版（本工程的复制来源） |
