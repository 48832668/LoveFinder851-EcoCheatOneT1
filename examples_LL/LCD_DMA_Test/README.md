# LCD_DMA_Test（LL 库 + C++17 + SPI1_TX → DMA1_Channel1）

ST7735 彩屏的 **DMA 加速**例程（LL 库 + C++17）。基础显示见同目录 `examples_LL/LCD_Test/`。

- **主控**：PY32F003F18U6-E（Cortex-M0+，64 KB Flash / 8 KB RAM）
- **屏幕**：ST7735 0.96" 160×80，SPI 接口
- **驱动库**：LL（`USE_FULL_LL_DRIVER`）+ `LoveFinderLibForPY32_LL`
- **语言**：C++17（`-std=gnu++17`，`v6LangP=9`），厂商源码仍按 C 编译
- **DMA**：SPI1_TX → **DMA1_Channel1**（PY32F003 靠 SYSCFG 重映射路由，见 §3）

---

## 1. 编译验证（Keil MDK 5.43a / Arm Compiler 6.24，`-O1` + LTO，clean rebuild）

```
Program Size: Code=11464 RO-data=2228 RW-data=216 ZI-data=3480
0 Error(s), 0 Warning(s)
```

| | Flash 合计 (Code+RO+RW) | RAM 合计 (RW+ZI) |
|---|---|---|
| 本工程 | 13908 B | 3696 B |

> ⚠️ 本工程比 HAL 版（12568 B）**大**，原因见 §5 —— 不是 LL 的问题，
> 而是换用了功能完整得多的 C++ 屏驱库（完整图形原语 + 7×10 字库 + UTF-8）。

---

## 2. 硬件连接

| 信号 | 引脚 |
|---|---|
| SPI1_MOSI | PA3 |
| SPI1_SCK | PA5 |
| LCD_DC | PA4 |
| LCD_RESET | PA6 |
| LCD_CS | PA7 |
| LCD_EN（背光） | PA12 |
| **SPI1_TX → DMA** | **DMA1_Channel1**（SYSCFG 重映射） |

---

## 3. DMA 实现要点

### 3.1 通道映射

PY32F003 的 DMA 请求**不是硬件固定的**，而是靠 `SYSCFG->CFGR3` 的三个 5 位字段
把"请求源"路由到通道 1/2/3（复位值全 0 = ADC）。SPI1_TX 必须显式映射：

```c
LL_APB1_GRP2_EnableClock(LL_APB1_GRP2_PERIPH_SYSCFG);          // 先开 SYSCFG 时钟！
LL_SYSCFG_SetDMARemap_CH1(LL_SYSCFG_DMA_MAP_SPI1_TX);          // SPI1_TX → 通道 1
```

`rm:` 诊断值（小结页打印）读 `SYSCFG->CFGR3 & 0x1F`，期望 **1**（= SPI1_TX）。
读到 0 说明重映射没生效（最常见是 SYSCFG 时钟没开，写入被丢弃）。

### 3.2 初始化位置

DMA 通道配置在 **`Studio_SPI1_Init()`（`Core/SRC/spi.cpp`）内部**完成，不是独立的
`Studio_DMA_Init()`。`Core/SRC/dma.c` 的 `Studio_DMA_Init()` 只负责开 DMA 时钟，
工程里并未调用它（时钟在 spi.cpp 里开）。

```c
Studio_GPIO_Init();
Studio_I2C1_Init();
Studio_SPI1_Init();     /* 内部含 SPI1_TX 的 DMA1_CH1 全部配置 */
Studio_USART1_Init();
Studio_TIM3_Init();
Studio_CRC_Init();
ST7735_Init();          /* 末尾做 DMA 启动自检 */
```

### 3.3 ⚠️ 手写 LL 的坑：必须设外设地址（CPAR）

spi.cpp 用的是**手写 raw setter** 而不是 `LL_DMA_Init()`，所以下面这一行**绝对不能省**：

```c
LL_DMA_SetPeriphAddress(DMA1, LL_DMA_CHANNEL_1, (uint32_t)&SPI1->DR);
```

**后果（曾真实踩过）**：漏掉时 CPAR = 复位值 0，DMA 会把数据写到内存地址
`0x00000000`，总线挂死，TC 永不置位 → 启动自检 50ms 超时 → `tx:0 f:1` →
`ST7735_Init()` 里 `s_useDma` 被永久置 0 → 之后"开 DMA"的环节其实全部走阻塞 SPI，
**FPS 和 CPU 阻塞一模一样**。诊断特征就是小结页的 `tx:0 f:1 rm:1`（rm 正确、tx 为 0）。

### 3.4 传输策略：轮询 + 超时 + 自动退化

沿用 HAL 版验证过的稳健设计（见 `LoveFinderLibForPY32_LL/ST7735/st7735.cpp`）：

1. **轮询 TC 标志 + 50 ms 超时**，不用中断 —— 不依赖 SRAM 向量表，最稳
2. **启动自检**：`ST7735_Init()` 末尾在 CS 未选中时试发 4 个哑字节
3. **自动永久退化**：任何一次超时 → `s_useDma = 0` → 之后全部走阻塞 SPI，
   画面照常，只是慢一点
4. **乒乓双缓冲**：`ST7735_DrawFrameRectEx(useDma=true)` 让 CPU 渲染第 N+1 行
   与 DMA 发送第 N 行重叠
5. **整帧重绘保护**：帧中途超时则重设地址窗口、用阻塞方式重绘整帧

> DMA 诊断计数：`ST7735_GetDmaTxOk()` 成功次数、`ST7735_GetDmaTxFail()` 失败次数。
> `tx` 一直为 0 → 实际走的是阻塞退化路径，DMA 没在工作。

### 3.5 乒乓双缓冲渲染管线

```c
fn(0, cur);  dma_start(cur);              // 发第 0 行
for (y = 1; y < H; y++) {
    fn(y, nxt);                           // CPU 渲染第 y 行
    dma_wait();                           // 同时 DMA 在发第 y-1 行  ← 重叠
    swap(cur, nxt);  dma_start(cur);
}
dma_wait();
```

缓冲：`static uint8_t s_fbuf[2][160 * 2]` = **640 字节 BSS**。

### 3.6 纯阻塞 vs "假阻塞"

- `ST7735_DrawFrameRectEx(..., useDma=false)` 内部走统一的 `st7735_send_data()`，
  只要库内 DMA 可用（`s_useDma==1`）就**依然会用 DMA**（逐行串行、无乒乓）。
- 做**真·DMA 关**对比要调 `ST7735_DrawFrameRectBlocking()`（新增，直接 `st7735_spi_write`），
  才是货真价实的 CPU 轮询阻塞 SPI。

---

## 4. 演示流程（4 个环节循环）

**布局**：状态栏只占 y=0..19；其余 y=20..80 整块 160×60 全是测试区。
状态栏第一行 `[n/4] 环节名`，第二行用 **绿 `DMA:ON` / 红 `DMA:OFF`** 标明当前路径。

**每个测试环节**：先 **DMA 关（纯阻塞）** 测 1 秒 → 再 **DMA 开** 测 1 秒 → 清屏出小结。

| # | 环节 | 内容 | 传输方式 |
|---|---|---|---|
| 1 | `FILL` | 全屏 160×60 大块填充（彩虹轮色） | `FillRectangle` / `FillRectangle_DMA` |
| 2 | `SHAPE` | **随机几何图形**：每帧随机 6 个图形（圆/矩形/圆角矩形/三角形/菱形），随机位置/大小/颜色，行渲染器逐点测试画满全屏，下一帧重随机=清屏重画 | `DrawFrameRectBlocking` / `DrawFrameRectEx(true)` |
| 3 | `FLAG` | **HelloWorld + 红旗飘扬背景**（计算密集） | 同上 |
| 4 | `SUMMARY` | 三环节 FPS / 加速比汇总 | — |

小结页打印：`noDMA FPS/ms`、`DMA FPS/ms`、`Speedup x.y`、DMA 诊断 `tx:f:rm`。

### 4.1 FILL —— 大块填充
单次传输数据量最大（160×60 = 19200 字节/帧），最能体现 DMA 在传输侧的价值。

### 4.2 SHAPE —— 随机几何图形
用软件行渲染器把随机图形合成进帧缓冲：每帧 160×60 次点测试（圆/圆角/菱形用整数
平方，三角形用整数半平面，**无除法无浮点**，Cortex-M0+ 没有硬件除法器）。
这是 CPU 密集环节，最能观察 DMA 乒乓"计算与传输重叠"的收益。

### 4.3 FLAG —— HelloWorld + 红旗飘扬
背景是红色基色 + 沿 `(x*3 + y*5)` 方向、随时间 `s_frame` 平移的三角波亮度，
看起来像红旗在飘。**文字永不被覆盖**：`TextPixel()` 用字库位图（`font_get_glyph`）
逐点判定，凡落在 "HelloWorld" 字模上的像素直接覆盖背景色，文字画在最上层。
文字居中 (45,25)，10 字符 × 7px = 70px 宽，Font_7x10 高 10px。
每帧 160×60 次像素计算（含字模查表）——计算量比 FILL 大得多。

### 4.4 预期结果
- **FILL**（纯传输）：DMA ≈ CPU —— 两者都被 12 MHz SPI 线速卡住，DMA 破不了线速。
- **SHAPE / FLAG**（计算密集）：DMA 乒乓应明显领先 —— CPU 算第 N+1 行时 DMA 在发第 N 行，
  每行耗时从「计算+传输」降为「max(计算,传输)」。
- 前提是 **`tx` 非 0**（DMA 真的在工作）。若 `tx:0 f:1`，回到 §3.3 检查 CPAR。

---

## 5. 为什么比 HAL 版大？（重要）

> 下表是**上一版（未开 LTO）**的逐模块统计。当前工程开启 LTO 后全部合并进单个
> `lto-llvm-*.o`，无法逐模块拆分；当前版本总体积以 §1 为准。

逐模块统计（`Code` 字节）：

| 模块 | 本工程(LL) | HAL 版 | 差异 |
|---|---|---|---|
| **LL / HAL 驱动** | **1776** | **5042** | **−3266** ✅ LL 仍然大胜 |
| 屏驱库 `st7735` | 6060 | 1168 | **+4892** |
| 字库 `font_data`(RO) | 1668 | 475 | +1193 |
| 演示代码 `main` | 2388 | ~1000 | +1388 |

**结论：LL 驱动本身依然省 3266 字节**，但被三处「功能差异」反超：

1. **屏驱库功能完整得多** —— 含完整图形原语、UTF-8、图标、快速填充路径。
   这些函数原先逐像素实现（每像素做一次完整的地址窗口设置），既慢又占空间。
2. **字库是 7×10（20 字节/字符）vs 5×7（5 字节/字符）** —— 字形质量高得多。
3. `snprintf` vs `sprintf`，以及演示场景更多。

> 这是**功能对比**，不是 LL vs HAL 的对比。纯驱动层面 LL 依旧大幅领先。

---

## 6. 字库：字符级编译

本工程只有一个 `LoveFinderLib/FontLib/font_config.hpp` —— 只声明「要编译哪些字」。
字形**像素数据在共享库** `LoveFinderLibForPY32_LL/FontLib/` 里，所有例程共用：

```c
#define USE_FONT_7X10        1
#define FONT_7X10_CHARS_STR  " %+,-./0123456789:=ACDEFGHILMNOPRSTUWXY[\\]abcdefghiklmnoprstuvwxz"
```

65 个字符，覆盖测试流程全部文字（状态栏 `[n/4]`、DMA:ON/OFF、measuring、HelloWorld、
FILL/SHAPE/FLAG/SUMMARY、小结与汇总表的全部字符）。

> ⚠️ 数字 0-9 必须全部列出 —— 性能数字是运行时 `snprintf` 生成的，源码里看不到，
> 漏掉就会显示成空白。增删显示字符时改这个清单，否则新字符会被静默跳过。

---

## 7. 相关文档

- `examples_LL/LCD_Test/README.md` —— 基础显示例程（同一套屏驱与字库）
- `LoveFinderLibForPY32_LL/README.md` —— 库的架构说明与移植点
- `tempLate_LL/` —— 新建工程的 LL 空工程母版（仅 `main` + 自定义库为 C++）
