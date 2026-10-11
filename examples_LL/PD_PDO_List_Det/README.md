# PD_PDO_List_Det（LL 库 + C++17 + FUSB302 纯 SINK + 160×80 屏显）

板载 **FUSB302** 做 USB PD **纯 SINK**，读出 Source 的 **Source_Capabilities**，
把 **PDO 列表**显示在板载 **ST7735 160×80** 屏上。

- **主控**：PY32F003F18U6-E（Cortex-M0+，64 KB Flash / 8 KB RAM）
- **屏幕**：ST7735 0.96" 160×80，SPI 接口（**SPI1_TX → DMA1_Channel1**）
- **PD 控制器**：FUSB302B，I2C1（PF0=SDA / PF1=SCL），7 位地址 **0x22**
- **驱动库**：LL（`USE_FULL_LL_DRIVER`）+ `LoveFinderLibForPY32_LL`（ST7735 / FontLib / FUSB302）
- **语言**：C++17（`-std=gnu++17`），厂商源码仍按 C 编译
- **不使用串口**：PDO 列表**只**显示在屏上，`Studio_USART1_Init()` 有意不调用

> 本工程的基础是 `examples_LL/LCD_DMA_Test`：同一套屏驱（共享 C++ ST7735 库 + DMA）、
> 同一套字库、同一份 `Core/` 外设初始化，只是把 `main.cpp` 换成了 PD 检测逻辑。

---

## 1. 编译验证（Keil MDK 5.43a / Arm Compiler 6.24，clean rebuild）

```
Program Size: Code=16188 RO-data=2796 RW-data=200 ZI-data=4032
0 Error(s), 0 Warning(s)
```

| | Flash 合计 (Code+RO+RW) | RAM 合计 (RW+ZI) |
|---|---|---|
| 本工程 | 19184 B | 4232 B |

> **Code 变小、ZI 变大是正常的**（第八轮）：`Row()` 被 LTO 从"内联进
> `$Super$$main`"改成**独立函数**（`_ZN12_GLOBAL__N_13RowEhPKctt`，
> 262 B），调用点不再各带一份内联副本 → `Code` −904 B；同时新增 252 B
> 行缓存（`g_rcTxt/g_rcFg/g_rcBg/g_rcOk`，9×28 B）→ `ZI` +256 B。
> 详见 §6g。
>
> 之后 §6g-补 又在开机处加了一段"预铺整帧"（`HeaderBar()` + `Row(0)` +
> 4 个空行 + 3 个开机提示行），`Code` 回到 **16188**（+220）。

> **⚠️ 编译选项必须与 `LCD_DMA_Test` 一致：`Optim 2`（-O1）+ `v6Lto 1`（LTO 开）。**
> 曾经用 `Optim 4`（-O3）+ LTO 关 编译出 `Code=19736`，
> 那版**烧进去只有背光、没有画面**；改回 `Optim 2 + LTO` 后与已验证的
> `LCD_DMA_Test` 完全同构（连 `__main` 里 0xC0 字节 RAM 向量表搬移都一模一样）。
> 开 LTO 后各 .o 合并进单个 `lto-llvm-*.o`，无法再逐模块拆分。

---

## 2. 硬件连接

| 信号 | 引脚 |
|---|---|
| SPI1_MOSI | PA3（AF10） |
| SPI1_SCK | PA5（AF0） |
| LCD_DC | PA4 |
| LCD_RESET | PA6 |
| LCD_CS | PA7 |
| LCD_EN（背光） | PA12 |
| **SPI1_TX → DMA** | **DMA1_Channel1**（SYSCFG 重映射，见 `spi.cpp`） |
| I2C1_SDA | PF0（AF12，开漏，400 kHz） |
| I2C1_SCL | PF1（AF12，开漏，400 kHz） |
| **FUSB302 INT** | **PA0**（`INT_N`，开漏**低有效** → EXTI0 **下降沿** + 内部上拉；中断驱动，见 §6c） |

**PD 电源从 TYPE-C 接口 2 输入**（板子供电口）。

---

## 3. 屏幕布局（22 列 × 8 行）

`Font_7x10` 每字符 **7px 宽 / 10px 高** → 160/7 = 22 列，80/10 = 8 行。

### 3.0 开机自检（**不依赖字库**）

`ST7735_Init()` 之后先**整屏刷三色**：红 150ms → 绿 150ms → 蓝 150ms。
这三个 `FillScreen` 完全不经过字库，所以能一刀切开两类故障：

| 现象 | 结论 |
|---|---|
| 看到红/绿/蓝闪 | 时钟、GPIO、SPI、DMA、面板初始化**全部正常**；问题只可能在文字渲染或 PD 逻辑 |
| 三色都不出现，只有背光 | 问题在 `ST7735_Init()` 之前或之中 —— 查编译选项、DMA/CPAR、面板接线 |

另外每个视图的**标题栏**用 `ST7735_FillRectangle` 实心填充 —— 同样是纯填充绘制，
不经过字库。

> 早期在右上角画过一个 8×10 的**心跳方块**（红/绿/蓝/黄循环，每 80ms 换一次），
> 用来肉眼确认 MCU 活着。**已删除** —— 现在有逻辑分析仪，直接抓 SPI 波形就能判断，
> 没必要在屏上占地方。

### 3.1 状态视图 —— 还没拿到 Source_Capabilities 时

作用：**一眼看出卡在哪一步**，不用接串口。

```
+----------------------+
|PD PDO DETECT         | 行0  标题（青字深底）
|                      | 行1..2
|WAIT ATTACH           | 行3  当前阶段
|                      | 行4..7
+----------------------+
```

行 3 的文案：

| 文案 | 含义 |
|---|---|
| `WAIT ATTACH` | 还没检测到源端（`TOGSS` 未落定） |
| `LISTENING...` | 已 attach，正在听源端广播 |
| `LISTEN (HR)...` | 刚收到 Hard Reset / VBUS 恢复，长听 2500ms |
| `SOFT RESET n/3` | 没听到，已发第 n 枪 Soft Reset 催它 |

> **按键之前**还有一屏"启动画面"（`App_Run()` 开头手写，不走 `DrawStatus()`）：
> 行3 `PD PDO LIST DET`（青）、行4 `PD STARTING..`（黄）、行6 空。
> 开机第一枪（Hard Reset）打完之后，行4 改成 `PRESS KEY`、行6 显示
> `waiting button`（灰），进入等按键。

业主要求"屏上只显示 PDO"，所以状态视图**只留一行提示**，其余行空白。
（以前那种 8 行自检面板已废弃。）

> **终态不在这里画**：催满 `PD_SR_MAX`(3) 枪 Soft Reset 仍没等到 caps 时，
> 直接切到 PDO 视图输出 `0 PDO`。这样"按下按键后屏上要么 `0 PDO`、
> 要么就是 PDO 列表"只有两个出口，不会出现第三种画面。
> （旧的 `NO SOURCE` 文案已随之删除。）

### 3.2 PDO 视图 —— 收到 Source_Capabilities 后

**屏上就是纯粹的 PDO 列表，从行 0 开始排，没有任何其它内容。**
（行 0 原来那条 `PDO n=4 CC1 VBUS1` 摘要行、以及底部的灰字诊断行
`Hdr 43A1 caps x1`，都已按业主要求删除。）

```
+----------------------+
|1 FIX 5.0V 3.00A 15W  | 行0  ← 反白高亮 = 申请成功的档位
|2 FIX 9.0V 3.00A 27W  | 行1
|3 FIX 12.0V 3.00A 36W | 行2
|4 PPS 3.3-11.0V 2.00A | 行3
|                      | 行4..7  空（抹掉上一帧可能更长的列表）
+----------------------+
```

一条 PDO 都没拿到时（源端始终没广播）：

```
+----------------------+
|0 PDO                 | 行0  红字
|                      | 行1..7  空
+----------------------+
```

| 类型 | 格式 |
|---|---|
| Fixed | `n FIX 12.0V 3.00A 36W` |
| Variable | `n VAR 3.3-12.0V 3.00A` |
| Battery | `n BAT 3.3-12.0V 30W` |
| PPS/APDO | `n PPS 3.3-11.0V 2.00A` |
| 解析失败 | `n raw XXXXXXXX`（红字） |

> 原来的 `Hdr`（源端报头）和 `caps`（收到几条 caps）两个值**仍然每轮采集**，
> 只是不再占屏幕：分别在 `g_snap[11..12]` 和 `g_snap[10]`，
> 需要时用 OpenOCD `mdw &g_snap 30` 读。

#### 高亮 = "5V 档真的申请成功了"

第 1 条 PDO 用**反白**（底 `C_SEL_BG = 0x11F3` 中蓝 + 白字）画，
条件不是"我们发了 RDO"，而是**对端回了 PS_RDY**：

```
SNK:Request → SRC:Accept(0x03) → SRC:PS_RDY(0x06)    → 反白（申请成功）
SNK:Request → SRC:Reject(0x04)                       → 不反白（被拒）
```

判定方式：`Accept`/`PS_RDY`/`Reject` 都是**控制报文**，
`pollSourceCapabilities()` 为了找 Source_Capabilities 会把它们取出来丢掉，
所以库新增了 `lastPeerMsgType()` —— 记下"最后一条被跳过的报文类型"。
上层读到 `0x06` 就把 `g_rdoAck` 置 1（并记一条 `L_PSRDY` 日志），读到 `0x04` 清 0。

**拔出后自动回到状态视图**（`TOGSS` 变回非 SNK 时清掉 PDO 列表和 `g_rdoAck`）。

---

## 4. 运行流程

```
上电
 └─ RCC 24MHz → GPIO → I2C1 → SPI1(含 DMA) → TIM3 → CRC
    → ST7735_Init()            屏驱库末尾做一次 DMA 启动自检
    → 开机画面 400ms           （确认屏幕亮 = MCU 在跑）
    → App_Run()：
         ① 点亮屏（行3 标题 / 行4 "PD STARTING.."）
         ② ★ 开机第一枪 —— pd.init() → startSinkToggle() → pollAttach()
            → selectCC() → sendHardReset()      （最多等 800ms 探 attach）
         ③ 等按键（行4 改成 "PRESS KEY"）→ 按下瞬间 = 日志时基零点
         ④ 主循环：
              1. pd.init(I2C1) 重试（仅当 ② 失败）      每 500ms
              2. attach / VBUS / "先听后催" 状态机      每 10ms
              3. 读 Status0 + 轮询 RX FIFO              每 5ms
              4. 读 Status0a / InterruptA/B（含 HARDRST） 每 20ms
              5. 屏刷新（DMA 乒乓）                     最快 80ms 一次
```

- **`pd.init()` 内部**：软复位 → 等待 2ms → 读 ID → Power 0x0F →
  Switches0 Rd 下拉 + MEAS_CC1 → Switches1 AUTO_CRC+SPECREV=Rev2.0+TXCC1 →
  Control0 = **0x0C** → Control3 → Mask1/A/B → Slice → 冲两个 FIFO。
- **★ 开机第一枪（2026-10-09 第三轮）**：程序一跑起来就发一条 **PD Hard Reset**
  （`Control3.SEND_HARD_RESET`）。FUSB302 由板上 3V3 供电、MCU 复位它并不掉电，
  源端也不知道我们复位过 —— 不主动清一次，它会一直沿用**上一个程序**谈好的合约。
  发完源端会撤 VBUS ~0.9s 再重播 caps，这段由 `PD_HR_RECOVER_MS=2500ms`
  的窗口屏蔽，不判拔出。**重播的 caps 一律忽略**：PDO 检测仍从按键才开始。
- **按键后的"先听后催"**：短听 250ms → 没 caps 就发 Soft Reset（最多 3 枪），
  每枪之间等 3000ms；收到 caps 立刻切 PDO 视图。
- **Hard Reset / VBUS 掉过又回来**：`pd.recover()` 做一次 `Reset.PD_RESET`
  并重钉 CC 路由，然后进入 2500ms 的"长听"（源端重启后约 1s 才开始重播）。
- **拔出判据**：VBUS 连续低 **1500ms** 才算真拔出（源端响应 Soft Reset 时
  会主动拉低 VBUS 约 0.9s，判据太短会误判）。

### 屏刷新改走 DMA（文字路径）

`Row()` 现在调 `ST7735_DrawFrameRect(0, y, 160, 10, RowRender)`，由驱动内部
的 **2×320B 乒乓行缓冲**发送：DMA 推第 N 行时 CPU 渲染第 N+1 行，计算与传输重叠。
整屏 `160×80×2B = 25600B`，12MHz SPI 下 **≈17ms**，已经贴着 SPI 带宽上限。

对照旧路径 `ST7735_WriteString → ST7735_WriteChar`：那是**逐像素一次 SPI 传输**
（每字符 7×10=70 次 + 每字符一次 `SetAddressWindow`），整屏 ~14256 次，实测
**≈80ms** —— 主循环 ~90ms 的周期几乎全是它，I²C 轮询只是搭它的车。
`HeaderBar()` 也一并换成 `ST7735_FillRectangle_DMA()`。

> ⚠️ **字模必须用 `font_get_glyph()` 取，不能写 `Font_7x10.data[...]`。**
> 本字库是"字符级编译"（`font_data.cpp` 里 `Font_7x10 = {7, 10, nullptr}`），
> `FontDef.data` **恒为 nullptr**，字模由 `font_get_glyph()` 内部 switch 分派。
> 直接解引用 `data` 是空指针解引用；更坑的是 LTO 看得见 `data` 是常量 nullptr，
> 会把整张字模表判定为"不可达"直接删掉（实测 RO-data 掉了 2.2KB，
> 从 2784 → 504），而编译期**依然 0 error 0 warning**，完全静默。
> 改回 `font_get_glyph()` 后 RO-data 恢复 2784，字模表（`g_7x10_cp32`…）重新入镜。
> `Row()` 里按字符预先查好 22 个指针（`g_rowGlyph[]`），不是每像素查一次。

> ☠️☠️ **`RowRender()` 里 `ci` 必须夹 `ci < COLS`，这行判断删不得！**
> （2026-10-09 实机踩坑：上电画面正常，**一按按键就死**，屏停在 `PRESS KEY`。）
>
> `w = 160` 像素、`FONT_W = 7` → 22 个字符只占 **154** 像素，
> `x = 154..159` 这 6 个"零头"像素算出 `ci = 22`，而 `g_rowGlyph` 只有
> `COLS = 22` 个元素（下标 0..21）→ **越界读一个指针**。
>
> 越界读到的正好是 `.bss` 里紧邻 `g_rowGlyph` 的 **`g_diag`**
> （map 实证：`g_rowGlyph @0x20000210` + 88B = `0x20000268` = `g_diag` 起始），
> 于是把 `g_diag` 前 4 字节 `{tried, ready, idValid, devId}` 拼成的 uint32
> 当成"字模指针"：
>
> | 时刻 | `g_diag` 前 4 字节 | 当成的指针 | 后果 |
> |---|---|---|---|
> | 开机 | `00 00 00 00` | `0x00000000` | 读地址 0（被别名到 flash 向量表，**读得动**）→ 不崩，只是行尾 6 像素是垃圾色 —— **开机画面看起来完全正常，掩盖了问题** |
> | `pd.init()` 成功后 | `01 01 01 91` | `0x91010101` | 解引用未映射地址 → **BusFault → `HardFault_Handler` 的 `while(1)`** |
>
> 于是：开机画面正常 → 按下按键 → `pd.init()` 填上 `g_diag` → 主循环第一帧
> `DrawStatus()` 里的 `Row(0,…)` 就崩 → **屏永久停在 `PRESS KEY`**。
>
> 修法：`if (ci < COLS) { …查字模… }`，那 6 个零头像素填背景色。
> 编译后等价于 `CMP x,#0x99; BHI <用背景色>`（`x ≤ 153 ⟺ x/7 ≤ 21`），
> 已在反汇编里核对过。

---

## 5. 与上一版（PyStudio 串口版）的差异

| | 上一版 | 本版 |
|---|---|---|
| 工程来源 | PyStudio 导出（C 结构 + 自带 `st7735.c`） | 基于 `LCD_DMA_Test`（共享 C++ 屏驱库） |
| 输出 | USART1 115200 打印 | **160×80 屏**，无串口 |
| 刷屏 | 阻塞 SPI | 屏驱库 DMA（自检失败自动退化阻塞） |
| 字库 | 95 ASCII（工程内提取） | 95 ASCII（共享 `FontLib`） |
| DMA 诊断 | 无 | `ST7735_GetDmaTxOk()/Fail()` 可用 |

---

## 6. 已知坑（重要）

1. **FUSB302 软复位后必须等芯片就绪再访问后续寄存器**。
   FUSB302 datasheet 的 SETUP 流程里 `SW_RES` 之后有建立时间要求。
   本版已在 `LoveFinderLibForPY32_LL/FUSB302/FUSB302.cpp` 的 `init()` 里
   补了 `LL_mDelay(2)`。
   > 症状：能读到 ID，但 Power/Switches 写入被丢弃 → CC 下拉和 Toggle 全不生效
   > → 永远等不到 attach，屏上停在 `CC --`。

2. **`Status1a.TOGSS` 在 bits[5:3]**（不是 [3:1]）。
   值：`101`=SNK1(CC1)、`110`=SNK2(CC2)。

3. **本板 ST7735 必须发 `INVON`**，否则白底黑字。
   共享库 `st7735.hpp` 里 `INVERT = true`（`ST7735::ROTATION = 0x68`）。

4. **DMA 手写 LL 配置必须设 CPAR**（`LL_DMA_SetPeriphAddress(..., &SPI1->DR)`），
   而且 **SPI1_TX 的通道映射要先开 SYSCFG 时钟**再写 `LL_SYSCFG_SetDMARemap_CH1()`。
   这两点 `Core/SRC/spi.cpp` 里都已经处理好了，改 `spi.cpp` 时别弄丢。

5. **`main.hpp` 必须包含 `py32f0xx_ll_spi.h`**（屏驱库要用），漏了会报一堆错。

6. `snprintf` 用的都是**整数**格式，没有浮点依赖。
   （Cortex-M0+ 没有硬件除法器，但这里每帧只有个位数次软除，可接受。）

7. **⚠️ attach 后必须 `pd.selectCC(cc)` 钉死 CC 路由（本工程已做）**。
   toggle 只负责用 Rd 探测插在哪根 CC 上（结果记在 `TOGSS`），
   而 **GoodCRC 从哪个引脚出去由 `Switches1.TXCCx` 决定** —— 两者是独立的开关。
   `init()` 时还不知道 CC 在哪，只能先写死 `TXCC1`；
   所以 attach 之后必须按 TOGSS 的结果重新指一次。

   > **不做的后果（实测抓包）**：CC 线上每 **~145 ms** 一组 **3 条** Source_Capabilities
   > （间隔 2.02 ms），而且**看不到我们的 GoodCRC** —— Source 插在 CC2 时，
   > GoodCRC 被发到没人听的 CC1 上，Source 收不到确认就重发 3 次，
   > 然后等 tTypeCSendSourceCap 再来一遍。协商永远完不成，VBUS 停在 5V。

8. **attach 之后的拔出判据是 VBUS 掉电，不是 TOGSS**。
   `selectCC()` 会停掉 toggle，TOGSS 从此冻结在最后一次落定值；
   本工程在已 attach 状态下改用 `Status0.VBUSOK` 判拔出，拔出后重开 toggle。

---

## 6b. ⭐ 2026-10-09 修复："广播期外按按键，发了 Soft Reset 也读不到 PDO"

> 依据：`C:/Debug/PDcap.csv`（PD 总线抓包）+ `C:/Debug/iiccap.csv`（FUSB302 I²C 抓包），
> 两份是**同一次运行**；另对照 `ON-FUSB302.pdf`（数据手册 Rev 1.12）与三个开源
> 参考实现（`FUSB302-master` / `pdsink` / `usb-pd-main`，在 `C:/Debug/Self/PD_FUSB302`）。

### 抓包时间线（按下按键 → 发 Soft Reset 之后）

| 时刻 | 事件 |
|---|---|
| 3860.72 ms | SNK 发 **SOFT RESET** |
| 3861.29 ms | SRC 回 **GOOD CRC**（(r1)） |
| 3867~3870 ms | SRC 回 **3 条 ACCEPT** —— 我们**一条都没确认** |
| **3871.69 ms** | **SRC 发 HARD RESET**（源端放弃） |
| 3921 ms | VBUS 被拉低 |
| 4786 ms | VBUS 恢复 |
| **4872~9656 ms** | SRC **重新广播 Source_Capabilities 约 5 秒**（每 ~149ms 一组、每组 3 条、msgId 相同） |
| 9730 ms | SRC 再发一次 HARD RESET |

而 I²C 抓包显示：**从 3871ms 之后到抓包结束，`Status1.RX_EMPTY` 恒为 1**，
一条报文都没进 RX FIFO。也就是说：

> 源端明明广播了 5 秒 caps，但 **FUSB302 根本没往 FIFO 里放任何一个字节**。

### 三个根因

**① FUSB302 收到 Hard Reset 后 PD 接收逻辑锁死，固件没做 `PD_RESET`。**
两家参考驱动都为此专门复位 PD 逻辑：
Chromium 的 `pd_reset()`（注释原文 *"bring the FUSB302 out of reset after Hard Reset"*）、
pdsink 的 `hr_cleanup() -> fusb_pd_reset()`（*"Cleanup internal states after hard reset
received or sent"*）。本工程原来从不写 `Reset.PD_RESET`，所以 RX 一旦锁死就再也醒不过来。
→ 新增 `FUSB302::pdReset()/recover()`，并在主循环轮询 `Status0a.HARDRST` 后调用。

**② `Control0` 写成 `0x0E`，`AUTO_PRE=1`，把硬件自动 GoodCRC 关掉了。**
数据手册 *PD Automatic Receive GoodCRC* 明写：自动回 GoodCRC 的条件是
**`AUTO_CRC=1` 且 `AUTO_PRE=0`**。`AUTO_PRE=1` 时芯片改去"收到好报文就从 TX FIFO
自动起发"。这就是"自动回包实测不生效"的真正原因 —— 而**软件补发是不可能的**：
PD 给 GoodCRC 的窗口只有几十 µs，我们的轮询最快也是毫秒级。
→ `Control0` 改回 **`0x0C`**，软件补 GoodCRC 的代码删除。

**③ `Switches1.SPECREV` 写了 `10`（Rev3.0），这是非法值。**
FUSB302B 是 **PD2.0 PHY**（ONSEMI 官网 Features："USB Power Delivery (PD) 2.0,
Version 1.1 Support"），数据手册对 bits[6:5] 只定义 `00`=Rev1.0、`01`=Rev2.0，
`10/11` 明写 **"Do Not Use"**。三个参考实现用的都是 Rev2.0。
→ `setSpecRev()` 现在会把对端的 Rev3.0 **钳到 Rev2.0**。

### 顺带修掉的时序问题

| 问题 | 原来 | 现在 |
|---|---|---|
| 按下按键后要等 2 秒才发 Soft Reset | `PD_LISTEN_MS = 2000` | **250ms**（源端每 ~150ms 广播一组，够判有无） |
| I²C 轮询间隔 20ms | `PD_RX_POLL_MS = 20` | **5ms** |
| attach/VBUS 轮询 50ms | `PD_ATTACH_POLL_MS = 50` | **10ms** |
| 中断闩锁轮询 200ms | `PD_INT_POLL_MS = 200` | **20ms** |
| VBUS 去抖 3×20ms=60ms | 会把源端 Soft Reset 引起的 **0.9s** VBUS 下跌误判成"拔出" | 时间型：连续低 **1500ms** 才算拔出；"掉过又回来"改为做 `recover()` |
| attach 后只在 CC 变化时才 `selectCC` | "拔出后用同一根 CC 重挂"会跳过，而 `MEAS_CCx` 同时是 **RX 选线** | attach 时**总是**重钉一次 |

### ❌ 更正：PPS **不会**消失（上一版判断错了）

上一版这里写"SPECREV 钳到 Rev2.0 后 PPS 会消失" —— **实测证明是错的**。
新抓包 `PD2CAP.csv` 显示源端**照样以 Rev3.0 广播完整的 4 个 PDO**：

```
[1] [Fixed] 5V 3A (15W) [unconstrained]
[2] [Fixed] 9V 3A (27W)
[3] [Fixed] 12V 3A (36W)
[4] [PPS]   3.3/11V 3A (33W)      ← PPS 正常出现
```

源端报头 `H:43A1` → 低字节 `0xA1` 的 bits[7:6] = `10` = **Rev3.0**。
caps 内容完全由源端决定，跟我们 GoodCRC 用哪个版本**无关**。

关键澄清（数据手册原文）：

> `Switches1` bits[6:5] SPECREV —— **"Bit used for constructing the GoodCRC
> acknowledge packet."**  `00`=Rev1.0  `01`=Rev2.0  `10,11`=Do Not Use

即 **SPECREV 只决定芯片自动回的 GoodCRC 报头**，**不参与**我们写进 TX FIFO 的报文 ——
我们自己组的报头原样发出。所以两个值必须分开存：

| 成员 | 用途 | 取值 |
|---|---|---|
| `m_rev` | `Switches1.SPECREV` → 自动 GoodCRC 的报头 | 钳到 `0..1` |
| `m_hdrRev` | 我们**发出去**的报文报头 | 允许 `0..2`（含 Rev3.0） |

这正是开源项目 **usb-pd-main** 的做法：`Switches1` 写 `0x25`（Rev2.0），
但把 `hdr_template` 升到 Rev3.0，用 FUSB302 跑 PD3.0 + PPS。
本库现在照做：`setSpecRev()` 同时更新这两个值。

### ★★ 新修复：收到 caps 后必须**立刻**回 Request(RDO)

**这是"VBUS 在 5V/0V 之间来回跳"的真正原因。**

源端发完 `Source_Capabilities`、并收到我们硬件自动回的 GoodCRC 之后，会启动
`tSenderResponse` 定时器（PD 规范 24~30ms）**等 SINK 的 Request**。
超时没等到 → 源端直接发 **Hard Reset** 并把 VBUS 拉低重启。

`PD2CAP.csv` 实测（同一次运行，31 条解码报文）：

```
#5  1167.448ms  SRC[1]: SOURCE CAP (4 PDO)
#6  1168.522ms  SNK[1]: GOOD CRC
#7  1197.187ms  HRST              ← 1168.522 + 28.66ms
#8  2199.597ms  SRC[0]: SOURCE CAP ← 源端重启后重播
#9  2200.670ms  SNK[0]: GOOD CRC
#10 2229.446ms  HRST              ← +28.78ms
... 一直循环（#11~#31 共 7 轮）
```

`IICAP2.csv` 也印证：**整段抓包我们只发过一条 PD 报文** ——
1159.391ms 那次 `CONTROL0 <= 0x0D`（`C0_BASE|TX_START`）的 Soft Reset。
之后再没出现过 `TX_START`，其余 `0x4C`/`0x0C` 全是 `pdReset()` 的冲 FIFO。
**我们从来没回过 RDO。**

修复：在收到 caps 的**同一轮**里，立刻发一条 `Request`，只申请第 1 个 Fixed 档位
（通常就是 5V），保持 VBUS 不变：

```cpp
uint32_t rdo = (pos << 28) | (cur10 << 10) | cur10;   /* pos=1, cur10 取源端 PDO 的电流 */
pd.sendRequest(rdo);
```

驱动侧新增 `sendData()` / `sendRequest()`（数据报文，报头 NDO=1 + 4 字节 RDO 小端）。
`pos`/`cur10` 从 `msg.pdo[]` 现取，源端报多少就申请多少，不写死。


### 还没做、但可以考虑的优化

- **RX FIFO 逐字节读**。`fetchMessage()` 目前每个字节一次 I²C 事务（~114µs），
  一条 23 字节的 Source_Caps 要 ~2.6ms。FUSB302 的 0x43 支持"连续读不自动加地址"
  （数据手册注 11），理论上可以**一次事务读完一整条报文**（~250µs，快 10 倍）。
  没做是因为 F1 风格 I²C 的多字节接收（`NACK`/`STOP`/`BTF` 的时序）在
  没有实机的情况下容易写错，而写错会**把 SCL 拉死**，风险大于收益。
  当前速率在功能上够用（80 字节 FIFO，源端每组 3 条 = 69 字节）。

### 另外两个原来就有的小 bug

- `Diag::capPrompt` 从来没被赋值，`DrawStatus()` 却拿它判断状态 —— 于是
  永远显示 `LISTENING...`。现在改成用真实的 `phase` / `srTries`。
- 一批死变量（`vbusLowCnt` / `lastSelCc` / `tAttachOk` / `SDAC 扫描梯` 等）已清理。

### Hard Reset 检测：为什么用 `INTERRUPTA.bit0` 而不是 `Status0a.HARDRST`

`INTERRUPTA.bit0`（`I_HARDRST`）是 **R/C（读即清）** 位 —— 读一次自动归零，
天然就是**边沿检测**，不会出现"状态位一直挂着、每轮都重复写 `PD_RESET`
反复复位接收前端"的副作用。实测这块芯片即使 `MASKA.bit0=1`（屏蔽 INT 脚），
闩锁照样会置位（抓包 4000.484ms 读到 `INTERRUPTA=0x01`），所以**只轮询**也能拿到。

`Status0a.HARDRST` 仍然每轮读进 `g_diag.status0a` / `g_snap[27]` 作诊断，
但不作为复活触发点 —— 否则它会在整个 Hard Reset 期间一直为 1，每 20ms 触发一次复位。

### ★ 2026-10-09 第二轮：`RowRender` 越界崩溃（"按按键就死"）+ UI 改版

**① 崩溃（已实机验证修复）**：上电画面正常，**一按按键就死**，屏停在 `PRESS KEY`。
根因是 DMA 文字渲染里 `g_rowGlyph[ci]` 的 `ci` 越界（`ci` 最大 22，数组只有 22 个元素），
越界读到的正是紧邻的 `g_diag`；开机时它是全 0 所以"看起来正常"，
`pd.init()` 填上 `devId=0x91` 后指针变成 `0x91010101` → BusFault → `HardFault_Handler`。
**详细推导 + 反汇编核对见第 4 章的"☠️☠️"警示框。**

**② UI 改版**（按业主要求，调试阶段基本结束，屏上要干净）：

- 删掉行 0 的 `PDO n=4 CC1 VBUS1` 汇总行 —— PDO 列表**从行 0 开始排**。
- 删掉底部的灰字诊断行 `Hdr 43A1 caps x1`（业主反馈"屏幕底部还有灰色字"）。
  这两个值仍每轮采集，只是不占屏：`g_snap[11..12]` / `g_snap[10]`。
- 一条 PDO 都没有时显示 `0 PDO`；催满 Soft Reset 的**终态也走这条路**
  （不再画 `NO SOURCE`，`DrawStatus()` 里那条死分支已删）。
- **第 1 条 PDO 反白高亮**（底 `C_SEL_BG=0x11F3` + 白字），
  条件是对端回了 `PS_RDY`（不是"我们发了 RDO"）。
- 驱动侧新增 `FUSB302::lastPeerMsgType()` / `clearLastPeerMsgType()`：
  `pollSourceCapabilities()` 会把 `ACCEPT`/`PS_RDY`/`REJECT` 取出来丢掉，
  现在把"最后一条被跳过的 msgType"记下来给上层判断申请结果。
- 新增日志码 `L_PSRDY(26)`；`g_snap[20]`（原心跳槽位）改记 `g_rdoAck`。

### ★ 2026-10-09 第三轮：Hard Reset 从"按键后"提前到"开机后"

业主实测反馈：**Hard Reset 要发生在"程序跑起来"的那一刻**，而不是按下按键之后。

场景（业主原话）：调试迭代期间 SRC 一直和 SINK 连着，复位/烧录期间源端一直在
按上一轮谈好的 5V 合约握手。等到按键才 Hard Reset，等于让上一个程序的合约
一直挂到那一刻才被清掉。

改动（`Core/SRC/main.cpp` 的 `App_Run()`）：

| | 改前 | 改后 |
|---|---|---|
| `pd.init(I2C1)` | 按键之后 | **开机立即** |
| `startSinkToggle()` / `pollAttach()` | 主循环里（按键后） | **开机**（最多等 `PD_BOOT_ATTACH_MS=800ms`） |
| `selectCC()` | 主循环 attach 分支 | **开机**，attach 时立刻钉 CC |
| `sendHardReset()` | attach 分支里（按键后） | **开机**，紧接 `selectCC()` |
| 重播的 caps | — | **忽略**（PDO 检测仍从按键开始） |
| 按键后短听的计时起点 | attach 分支设 `tPhase` | 按键那一刻 `tPhase = g_log_t0` |

⚠️ **必须补的一句**：`attached` 开机就置 `true` 之后，主循环**不会**再走 attach
分支（那里原本会 `tPhase = now`）。若不补 `tPhase = g_log_t0`，`tPhase` 仍是 0，
`now - tPhase` 瞬间 ≥ 250ms → **"先听"被跳过、直接发 Soft Reset**。
这正是业主这次要用逻辑分析仪验证的点，绝不能错。

⚠️ **另一个必须补的动作**：开机那发 Hard Reset 之后，FUSB302 的 PD 接收逻辑会
锁死（`Status1.RX_EMPTY` 恒 1）。所以按键之后、进主循环之前补一次 `pd.recover()`
（= `Reset.PD_RESET`）。此时离发出 HR 已经过去很久，不会打断报文。

日志/常量变化：

- `L_HRSND(27)` 现在落在**开机段**；`L_ATTACH` 的 `arg0 = 0` 表示
  "**开机探测没等到 attach**"（没插线？）。
- 开机段的日志（`L_BOOT/L_LCD/L_INIT/L_DEVID/L_TOGGLE/L_ATTACH/L_SELCC/L_HRSND`）
  时间戳是**相对 MCU 上电的绝对毫秒**；按键之后才相对按键 —— 日志里能看到这个断点。
- 新增 `PD_BOOT_ATTACH_MS = 800u`：开机探 attach 的最长等待，超时就先跳过
  （不阻塞进 PRESS KEY），由主循环的常规 attach 流程接手。

**验收（逻辑分析仪）**：

| 场景 | 预期 |
|---|---|
| 复位后立刻抓 PD | 总线**第一帧**是 `SNK → Hard Reset`（不再是 `SNK:softreset`） |
| 开机日志 | 出现 `L_HRSND(27) arg0=1`；随后的 `L_INTA` 里应含 `0x08`（`I_HARDSENT`）= 芯片确实把它发上线了 |
| VBUS | Hard Reset 后掉约 0.9s 再回 5V（2500ms 窗口内不判拔出） |
| 开机 5s 内按键 | 先听 250ms 就拿到 caps → 直接出 PDO 列表，**不发** Soft Reset |
| 开机 5s 后按键 | 先听 250ms 无 caps → 发 Soft Reset → 源端重播 → 出 PDO 列表 |

### 编译验证

```
Program Size: Code=16872 RO-data=2784 RW-data=200 ZI-data=3776
0 Error(s), 0 Warning(s)
```

（`Code` 增长轨迹：11464 → 13764 → 13796（`INTERRUPTA.bit0` 边沿检测）
→ 14064（`sendData`/`sendRequest` + RDO）→ 14108（删心跳残留）
→ 15684（文字走 DMA：`ST7735_DrawFrameRect` 乒乓 + 行回调）
→ 15616（UI 改版：删汇总行 + `0 PDO` + PS_RDY 高亮）
→ 15444（再删底部灰字诊断行 `Hdr … caps xN`）
→ 15668（Hard Reset 加在 attach 分支）
→ 16156（Hard Reset 提前到开机 + 开机段日志 / 按键后 `recover()`）
→ 16408（中断驱动：EXTI0 ISR 置标志 + 补读 `Interrupt(0x42)` +
重新武装 + 去掉冗余 `rxEmpty()` 二次读。`ZI` 3760 → **3776** = 新增
6 个 `extern "C"` 诊断量 16B）
→ 16648（I²C 突发读：新增 `readBytes()` + `fetchMessage` 两段突发读 +
中断块合并成 7 字节突发读 + 删掉非 caps 路径的 `rxFlush`。见 §6d）
→ 16836（界面行为：内容不变不重画（`UiFp` 指纹）+ `DrawStatus()`
补齐 1/2/4/5/6/7 行擦除 + `g_snap[]` 移出重画条件 + 2s 看门狗。
见 §6e。**其中"2s 整屏看门狗"在第八轮改成了"500ms 单行"，见 §6g**）
→ 16872（修 `readBytes()` 收尾时序：每次 burst 多读 1 个字节 → 循环上界
2→3、STOP 挪到最后一个字节到齐之后。见 §6f）
→ **15968**（行级缓存 + 看门狗从"整屏 2s"改成"单行 500ms" + 删中断块里的
`dirty`。见 §6g。**注意 Code 是往下走的** —— `Row()` 被 LTO 改成独立函数，
省掉了 4 处内联副本；`ZI` 反而 3776 → **4032**（+256 = 252B 行缓存）。
→ **16188**（§6g-补：开机就预铺整帧，消掉"按键后第一帧 35ms"那个窗口。
`RO` 2784 → 2796）。

---

## 6c. ★ 2026-10-09 第四轮：中断优化（把 INT_N 真正用起来）

**背景**：FUSB302 的 INT 脚早就接在 PA0 上、EXTI0 也开了 NVIC，但 ISR 里只清
标志、什么都不做 —— 整个 PD 业务全靠主循环轮询。业主提出"优化中断，尽量减少
轮询"。查下来发现这条线不只是"没用"，而是**坏了**，而且是四处叠加。

### 四处缺陷

| # | 位置 | 现象 | 修法 |
|---|---|---|---|
| ① | `gpio.cpp` PA0 | 配成 `LL_GPIO_PULL_NO`，但 INT_N 是**开漏低有效**（手册 p.5），没有上拉时"高"就是悬空 | 改 `LL_GPIO_PULL_UP` |
| ② | `gpio.cpp` EXTI0 | 配成 `TRIGGER_RISING`，可有效事件是**下降沿** —— 正好反了 | 改 `TRIGGER_FALLING` |
| ③ | `py32f003_it.cpp` ISR | 只 `LL_EXTI_ClearFlag`，不做任何 PD 工作 | 置 `volatile g_pdIrq`，**I²C 一律留在主循环** |
| ④ | `main.cpp` 主循环 | **`Interrupt`(0x42) 从来没被读过** | 补 `readReg8(INTERRUPT, i0)` |

**④ 是最坑的一个。** `Mask1` 里 `M_CRC_CHK` 是放开的（`0xEF` 的 bit4=0），
也就是说 `I_CRC_CHK`(0x42.bit4) 能拉低 INT_N。而 0x42 / 0x3E / 0x3F 是三个
**互相独立**的 R/C 寄存器：

```
INT_N = ((0x42 & ~Mask1) | (0x3E & ~Maska) | (0x3F & ~Maskb)) != 0
```

只读 0x3E/0x3F 是清不掉 0x42 的。于是第一包有效报文一到 → 0x42 置位 →
INT_N 拉低 → **永远清不掉，引脚永久趴在低电平**。
后果：如果只修 ①②③，接上中断脚会收到**恰好一次**边沿，之后永久静默。

> 这也解释了为什么"当年改成轮询"是当时唯一能走通的路 —— 不是不想用中断，
> 是这条线本身就不通。

### 改了什么

**1) 通路（三处，见上表 ①②③）**

ISR 现在只有两行有效代码：

```c
g_pdIrq = 1u;
g_pdIrqCnt++;
```

**为什么绝不在 ISR 里做 I²C**（三条，缺一不可）：

- 库里每次 I²C 访问带 20ms 超时轮询，而 ISR 优先级是 0 → 会掐死 SysTick，
  `uwTick` 停走 → 所有 `BSP_GetTick()` 计时（先听后催、28ms 红线、VBUS 去抖）
  全部失真；
- I²C1 是主循环和 ISR 共用的，主循环可能正卡在多字节传输中间，重入会写坏时序；
- 没必要 —— 中断的价值只是"立刻知道有事"，真正的处理本来就必须在主循环
  按 PD 状态机做。

**2) 放开需要的事件**（`FUSB302.cpp` 的 `init()`）

| 寄存器 | 旧 | 新 | 放开的事件 |
|---|---|---|---|
| Mask1 | `0xEF` | **`0x6F`** | `M_CRC_CHK`(收到有效包) + `M_VBUSOK`(VBUS 变化) |
| Maska | `0xEF` | **`0xA2`** | `M_HARDRST` + `M_TXSENT` + `M_HARDSENT` + `M_RETRYFAIL` + `M_TOGDONE` |
| Maskb | `0xFF` | `0xFF` | 全屏蔽（`I_GCRCSENT` 与 `I_CRC_CHK` 重复，放开只会让中断翻倍） |

> ⚠️ **关键事实**：`0x3E`/`0x3F` 的闩锁**即使被 Mask 屏蔽也照样会置位**
> （只是不会拉低 INT_N）。所以"改成中断驱动"对主循环读到的内容是**纯增量**，
> 原有判据一个都没变 —— 这是这次改动风险低的主要原因。

**3) 主循环：事件驱动 + 心跳兜底**

三个定时块的条件从 `(now - tX) >= 周期` 改成 `irqSet || (now - tX) >= 周期`。
周期值**语义变了** —— 不再是"轮询周期"，而是"心跳兜底"：

| 常量 | 值 | 含义 |
|---|---|---|
| `PD_ATTACH_POLL_MS` | 10 ms | attach(TOGSS) 兜底 |
| `PD_RX_POLL_MS` | 5 ms | RX 兜底：**协商中**（还没 PDO） |
| `PD_RX_IDLE_MS` | **100 ms** | RX 兜底：**已拿到 PDO / 已放弃** ← 新加，20 倍降幅 |
| `PD_INT_POLL_MS` | 20 ms | 中断闩锁兜底 |

**为什么心跳不能删**：边沿是会丢的。INT_N 是**电平型闩锁**，服务期间又来一包时
引脚本来就是低的，不会产生新下降沿。没有心跳，程序会**安静地卡死**；有心跳，
它只是退化成"慢一点但还活着"。

**为什么协商期仍保持 5ms**：源端发完 caps 只等 24~30ms 就要 Request
（`tSenderResponse`），超时直接 Hard Reset。兜底路径必须明显小于 24ms。
拿到 PDO 之后没有实时性要求了，才放宽到 100ms。

**4) 循环末尾"重新武装"**

```cpp
if (g_diag.ready && (LL_GPIO_IsInputPinSet(FUSB_INT_Port, FUSB_INT_Pin) == 0u)) {
  if (g_pdIrq == 0u) { g_pdIrqRearm++; }
  g_pdIrq = 1u;
}
```

服务完回读 PA0 电平，还是低就再触发一轮。**这一步不是锦上添花**：源端广播
caps 时是一组 **3 条连发**（组内间隔 ~2ms），几乎必然踩到"服务期间又来了新包"
的窗口，少了它"先听后催"里的"听"会随机丢包。

不会死循环：被放开的 7 个位**全部**落在主循环会读的 0x42/0x3E/0x3F 里，
一次完整服务后引脚必然抬回高电平。

**5) 顺手去掉一次冗余 I²C 读**

原来 `main.cpp` 读完 `STATUS1` 存进 `g_diag.status1`，紧接着又调 `pd.rxEmpty()`
—— 而 `rxEmpty()` 内部**再读一次 STATUS1**。现在直接用第一遍的快照判断，
一次 RX 轮询从 3 次读降到 2 次。

### 效果

| | 改前 | 改后 |
|---|---|---|
| 报文到达 → 处理 | 最坏 5 ms（轮询周期） | **~100 µs** |
| `tSenderResponse` 24ms 红线余量 | 5 倍 | **~240 倍** |
| Hard Reset → `recover()` | 最坏 20 ms | **~100 µs** |
| VBUS 变化 → 状态机响应 | 最坏 10 ms | **~100 µs** |
| 稳态 I²C 轮询 | 5 ms 一次 | 100 ms 一次（且实际不触发） |

> **说句实话**：省下来的 CPU 其实不多。I²C 跑 400 kHz，一次读 ≈60µs，
> 原来 5ms 轮询 3 次 + 20ms 轮询 3 次 ≈ **4% CPU**，改完大约省 3%。
> 真正的收益是**延迟**（上表前三行）和**把"INT_N 永久拉低"这个雷排掉**。

### 新增诊断量（`extern "C"`，OpenOCD 直接读）

| 变量 | 读法 | 含义 |
|---|---|---|
| `g_pdIrqCnt` | `mdw <g_pdIrqCnt> 1` | ISR 累计进入次数 |
| `g_pdIrqRearm` | `mdw <g_pdIrqRearm> 1` | 服务期间漏掉的边沿，靠回读引脚补触发 |
| `g_pdRxIrq` | `mdw <g_pdRxIrq> 1` | RX 服务由**中断**触发 |
| `g_pdRxHb` | `mdw <g_pdRxHb> 1` | RX 服务由**心跳**触发 |
| `g_pdIntReg` | `mdb <g_pdIntReg> 1` | 最近一次 `Interrupt(0x42)` 快照 |

`g_snap[30]` / `g_snap[31]` 也放了 `g_pdIrqCnt` / `g_pdRxHb` 的低字节。

**判读方法**：正常情况 `g_pdRxHb` 应该**一直是 0**（中断先到，心跳根本轮不上）。
若它跟着 `g_pdRxIrq` 一起涨 → 说明边沿没来，心跳在硬扛 → 回头查 PA0 上拉 /
EXTI 触发沿 / 0x42 有没有被读。

### 新增日志码

`L_INT0 = 28` —— `Interrupt(0x42)` 快照，**已剔除 `I_CRC_CHK`**。它每收到一包
就置位一次，日志环形区只有 64 条，记它会把开机/协商那段有用的记录全冲掉；
剔掉之后剩下的（`I_VBUSOK` / `I_ALERT`…）都是低频事件。

### 二进制级验证（`fromelf -c` 反查）

| 检查项 | 证据 |
|---|---|
| ISR 真的写标志 | `0x080006f2: STRB r1,[r2,#0]`（`r2 = 0x20000711 = &g_pdIrq`），随后 `ADDS`/`STR` 累加 `g_pdIrqCnt` |
| EXTI0 真的改成下降沿 | 栈上 `EXTI_InitStruct` 常量 `0x00020001` → `LineCommand=01`(ENABLE) / `Mode=00`(IT) / **`Trigger=02`(FALLING)** |
| PA0 真的上拉 | `LL_GPIO_Init` 调用处 `Pin=1`(PA0) 且 `Pull=1`(PULL_UP) |
| Mask 值真的改了 | `MOVS r2,#0xa; MOVS r3,#0x6f` / `MOVS r2,#0xe; MOVS r3,#0xa2` / `MOVS r2,#0xf; MOVS r3,#0xff` |
| 0x42 真的被读了 | `MOVS r2,#0x42` → `BL readReg8`，紧跟 0x3e / 0x3f / 0x3c 之后 |

### 逻辑分析仪验收点

1. **PA0 不再永久为低**：跑起来、等第一包 caps 过去之后量 PA0，应该是**高**
   （有报文时才脉冲式拉低）。改动前它会在第一包之后一直贴 0V。
2. **边沿次数对得上**：`g_pdIrqCnt` 的增量应约等于源端广播的报文条数
   （一组 3 条 caps → 至少 3 次边沿；GoodCRC 不算，因为 `I_GCRCSENT` 是屏蔽的）。
3. **心跳不顶事**：`g_pdRxHb` 保持 0（或极小），`g_pdRxIrq` 持续增长。
4. **先听后催逻辑没被破坏**：开机后 5 秒内按按键 vs 5 秒后按按键，行为应与
   第三轮完全一致（有广播直接读、没广播才发 Soft Reset）。

---

## 6d. ★ 2026-10-09 第五轮：I²C 读写优化（突发读）

**起因**：第四轮改完之后功能正常，业主给了一份 Saleae I²C 抓包（`C:/Debug/IIC3.csv`）
要求复查读写。抓包共 **980 条事务 / 6.711 s / 0 告警**，功能全对，但暴露出
三处浪费和一处冗余。

### 抓包体检结论（先说好消息）

| 检查项 | 抓包证据 |
|---|---|
| 新的 Mask 真的写进芯片 | 第 8~10 笔：`W 44:0A 6F` → `W 44:0E A2` → `W 44:0F FF` |
| `Interrupt(0x42)` 真的被读了 | **181 次**（第四轮之前是 **0 次**） |
| **中断真的把主循环提前叫醒了** | 协商期 `STATUS0` 心跳是 5 ms，但三包报文到达时的读取间隔是 **1.583 / 40.952 / 39.780 ms** —— 都不是 5 ms 的整数倍，只能是 ISR 触发 |
| 中断确由收包引起 | 1972.768 ms 读到 `STATUS0 = 0xD2`，bit6 `ACTIVITY = 1`（手册：CC 线上 ≥3 次跳变） |
| 开机 Hard Reset 已提前 | `W 44:09 40`（Control3.SEND_HARD_RESET）在 **t = 9.234 ms** |
| 按键门控有效 | 之后 **1884 ms 总线完全静默**（`WaitForButton()`），按键后 `W 44:0C 02`（`Reset.PD_RESET` = `recover()`） |
| 协商完整 | Source_Cap(4 PDO) → Request → GoodCRC → ACCEPT → PS_RDY 五条全在 |

源端广播的 4 个 PDO 解出来是：**5V/3A、9V/3A、12V/3A、PPS 3.3~11V/3A**；
我方回的是 `RDO = 0x1004B12C` → objPos 1、5 V / 3.0 A。

### 事务去向（改动前）

| 去向 | 条数 | 占比 |
|---|---:|---:|
| 中断块 `0x3E/0x3F/0x3C/0x42`（**分 4 次读**） | **724** | **73.9%** |
| `STATUS0`/`STATUS1` 心跳轮询 | 129 | 13.2% |
| RX FIFO **逐字节**弹出 | 44 | 4.5% |
| `STATUS1A` attach 轮询 | 37 | 3.8% |
| 寄存器写 | 37 | 3.8% |
| 其它寄存器读 | 9 | 0.9% |

### 改了什么

| # | 位置 | 改前 | 改后 |
|---|---|---|---|
| 1 | `FUSB302.cpp` `fetchMessage()` | 每字节一次 `popFifo()`，一条 4 PDO 的 caps = **23 条事务 ≈ 2.6 ms** | 3 字节 + 20 字节 = **2 条事务 ≈ 0.6 ms** |
| 2 | `FUSB302.cpp` `pollSourceCapabilities()` | 内部 `rxEmpty()` 再读一次 `0x41` | 新增 `fifoKnownNonEmpty` 形参（默认 false，**老调用方不受影响**），主循环传 true 跳过第 1 轮自检 |
| 3 | `main.cpp` 非 caps 路径 | `pd.rxFlush()` 空转 3 笔，且**带丢包竞态** | **删除** |
| 4 | `main.cpp` 中断块 | `0x3E`/`0x3F`/`0x3C`/`0x42` 分 **4 条**事务 | 从 `0x3C` 起 **1 条 7 字节**突发读 |

新增低层原语 `FUSB302::readBytes(reg, buf, len)`（`FUSB302.hpp` / `.cpp`）。

### 依据（数据手册原文，不是猜的）

* **p.11 Figure 14「I2C Read Example」** 画的就是多字节读：
  `S WR A [Register Address K] A S RD A [Read Data K] A [Read Data K+1] … NA P`，
  并注明 *"Single or **multi byte read** executed from current register location"*
  → 读地址之后**芯片内部寄存器指针自动 +1**。
* **寄存器表注 11** 单独说明 FIFO 是例外：
  *"FIFO register is serially read/written **without** auto address increment."*
  → 连读 `0x43` 时指针不动，每读一次弹一个字节。这正是搬 PD 报文要的语义。

### 收尾时序（v1 型 I²C 的经典坑）

N>2 时**不能**一边等 RXNE 一边读到底 —— 最后一个字节会因 ACK/STOP 来不及设置
而被重复收一次。按 RM0008「主接收 Method 1」：

1. `ACK=1` → 清 `ADDR` → 先把**前 N-2 个**字节读走；
2. 等 `BTF=1`（DR 和移位寄存器都满 → 后面正好剩 2 个）；
3. `ACK=0`；4. 读倒数第 2 个；5. 挂 `STOP`；6. 读最后一个。

本工程实际只会用到 `N=3`（token+header）和 `N=4..32`（数据对象+CRC）。

### 效果

| 指标 | 改前 | 改后 |
|---|---:|---:|
| 中断块事务数 | 724 | **181** |
| 一条 Source_Capabilities 的 I²C 时间 | ≈ 2.6 ms | **≈ 0.6 ms** |
| 每次收包的冗余 `STATUS1` 读 | 1 条 | **0 条** |
| 非 caps 路径的 `rxFlush` | 3 条 | **0 条** |
| 预计总事务数 | 980 | **≈ 400** |
| `Code` | 16408 | **16648**（+240 B） |

省下的 2 ms 全部落在源端 `tSenderResponse` 窗口（24 ms）里。

### 二进制级验证（`fromelf -c`）

| 检查项 | 证据 |
|---|---|
| `readBytes` 真的被调用 | 3 个调用点：`0x08002f1a` / `0x08002f78`（`fetchMessage` 两次）、`0x08003320`（中断块） |
| FIFO 突发读的寄存器号 | 两处都是 `MOVS r2,#0x43` |
| FIFO 第二段的长度 | `LSLS r0,r5,#2` + `ADDS r0,r0,#4` = **`ndoUse*4+4`**，与源码 `nBody` 一致 |
| 中断块合并成一次读 | `MOVS r2,#0x3c` + `MOV r3,r5`（`&st[0]`），随后 `uint8_t st[7]={0}` 的 `STR/STRH/STRB` 清零 |
| 合并后的取值下标 | `LDR r0,[sp,#0x180]`→`STRB r0,[r7,#0xf]`（`g_diag.status0a = st[0]`）；`LDRB r1,[r5,#6]`→`g_pdIntReg`（`st[6]` = 0x42） |
| 收尾时序 | `LSLS #24/BMI`（RXNE）、`LSLS #29/BMI`（BTF）、`LSLS #10/BICS`（清 CR1 bit10 = NACK）—— ⚠️ **这一版的收尾时序是错的，见 §6f** |
| 低层调用次数下降 | `BL readReg8` **26 → 13**，`BL writeReg` **34 → 32**（正好等于删掉的 `rxFlush`） |

### 逻辑分析仪验收点

1. **一条 caps 从 23 笔变 2 笔**：抓一次广播，`W 44:43` 后面应该跟一条
   `W 44:43 | R 45->…` 读 3 字节、再一条读 20 字节，中间没有 23 条碎读。
2. **中断块从 4 笔变 1 笔**：`W 44:3C | R 45->…` 一次返回 7 个字节
   （`3C 3D 3E 3F 40 41 42` 各一个）。
3. **总事务率下降**：空闲期应从 ~146 条/秒降到 **~60 条/秒** 量级。
4. **功能不回退**：PDO 列表照常显示、`g_rdoAck` 照常置位、0 告警。

> ⚠️ **事后补充（第六轮后发现的坑）**：上面第 1、2 条当时只数了"事务条数"，
> **没数每条读事务里的字节数**。实际这一版每条 burst 都多读了 1 个字节 ——
> 第 2 条那个 7 字节读实际读了 8 个，多出来的正好是 `0x43 = FIFOS`。
> 详见 §6f。**以后验收必须同时数字节数，不能只数事务数。**

### ⚠️ 本轮暴露出的界面行为问题（已在第六轮修掉）

屏每 80 ms **无条件**重画一次（`main.cpp` 的 `dirty || (now - tUi) >= PD_UI_MIN_MS`），
阻塞约 40 ms。两个指纹：

* 中断块读取间隔是 **19.7 / 19.9 / 40.8 ms** 循环，周期 **80.4 ms** ≈ `PD_UI_MIN_MS`；
* 空闲期 RX 心跳名义 100 ms，实测 **99.87 / 100.47 / 121.18 ms** 循环
  （周期 321 ms ≈ 4×80），那个 121 就是"有一拍 20 ms 被重绘吃掉"。

即主循环约有**一半时间**在重画一张没变化的屏。这条属于**界面行为**改动，
当时需要业主确认后再动 —— **业主已确认，第六轮已实现**，见下一节。

---

## 6e. ★ 2026-10-09 第六轮：界面行为改动（内容不变不重画）

### 起因

第五轮把 I²C 事务从 980 条压到 ~400 条之后，**剩下的瓶颈变成了屏**：
`dirty || (now - tUi) >= PD_UI_MIN_MS` 里那个周期性项，不管屏上内容有没有变，
每 80 ms 就把整屏重画一遍（`HeaderBar` 160×10 填充 + 8 行 × 160 像素 ≈ 25.6 KB SPI），
把 I²C 心跳顶掉。业主指示：**这个界面行为改动也做**。

### 改动清单

| # | 位置 | 改动 |
|---|---|---|
| 1 | `main.cpp` `App_Run` 第 4 块 | 把 `if (dirty \|\| (now - tUi) >= PD_UI_MIN_MS)` 换成**指纹比对**：内容变了才画 |
| 2 | `main.cpp` 新增 `struct UiFp` | "屏上画的是什么"的完整描述（7 个字段 + `pdo[7]`） |
| 3 | `main.cpp` `DrawStatus()` | 补齐 1/2/4/5/6/7 行擦除 —— 视图切换必须**完整**盖掉上一帧 |
| 4 | `main.cpp` 常量区 | `PD_UI_MIN_MS` 语义改为"最小间隔（限速）"；新增 `PD_UI_WATCHDOG_MS = 2000` |
| 5 | `main.cpp` `App_Run` 第 4 块 | `g_snap[]` 快照**移出**重画条件，改成每轮都写 |

### 1) 指纹：判"内容变了没有"

指纹必须覆盖三个画图函数的**每一个输入**，漏一个就会出现"内容变了屏上没跟着变"：

| 字段 | 来源 | 影响 |
|---|---|---|
| `view` | `havePdo` / `(phase == PH_GIVEUP)` | 选 `DrawPdoList` / `DrawNoPdo` / `DrawStatus` |
| `cc` | `g_diag.cc` | 状态页 `WAIT ATTACH` |
| `phase` | `g_uiPhase` | 状态页 `LISTENING...` / `LISTEN (HR)...` |
| `srTries` | `g_uiSrTries` | 状态页 `SOFT RESET n/3` |
| `rdoAck` | `g_rdoAck` | PDO 页第 1 条是否反白 |
| `pdoCount` | `lastMsg.pdoCount` | PDO 页画几行 |
| `pdo[]` | `lastMsg.pdo[i]` | PDO 页每行的内容 |

### 2) 门控：`(changed && minGapOk) || watchdog`

```cpp
const bool minGapOk = (now - tUi) >= PD_UI_MIN_MS;      /* 80ms 限速 */
const bool watchdog = (now - tUi) >= PD_UI_WATCHDOG_MS; /* 2s 兜底 */

if ((changed && minGapOk) || watchdog) {
  tUi = now; dirty = false; uiFpLast = fp; uiFpValid = true;
  ... 三个画图函数之一 ...
}
```

三点设计说明：

* **`dirty` 一个都没少。** 它现在是"强制重画"开关，各事件点（init / attach /
  拔出 / VBUS 恢复 / 收到 caps / 发出 RDO / PS_RDY / REJECT / Soft Reset /
  GIVEUP / Hard Reset 恢复 / 中断闩锁有内容）照旧置位。也就是说
  **事件驱动的刷新一条都没被砍掉**，砍掉的只是"没事也画"。
  指纹在这里是**冗余的安全网**：以后若新增一处会改屏上内容却忘了置 `dirty`
  的地方，指纹会替它兜住。
* **80 ms 下限为什么还留。** 指纹只回答"要不要画"，不回答"能不能立刻画"。
  协商期间 `dirty` 会被连发报文置得很密，有下限才不会把主循环刷成
  "只顾画图、顾不上收报文"。被下限挡下时**不更新 `uiFpLast`、也不清 `dirty`**，
  所以下一轮过了下限一定会补画 —— 不丢帧，只晚几十毫秒。
* **2 s 看门狗为什么还要。** 纯"指纹 + dirty"有个死角：SPI/DMA 偶发抽风把某行
  画坏了、而之后内容再没变过，那行就永远错着（旧版靠 80 ms 重画自愈）。
  留一条慢速兜底把自愈能力保住，代价是 SPI 占用从 12.5 次/秒降到 0.5 次/秒。

### 3) `DrawStatus()` 补齐擦除（**必需**的配套改动）

`DrawPdoList()` 会把 8 行**全部**写满（PDO 行 + 末尾补空行），
而 `DrawStatus()` 原来只用第 0 行和第 3 行。于是：

* 从 PDO 列表切回状态页（唯一路径是拔出：`havePdo → false`）时，
  第 1/2/4/5/6/7 行还挂着**上一帧的 PDO 文字** —— 屏上同时出现
  `WAIT ATTACH` 和旧的 `2 FIX 9.0V 3.00A 27W`；
* 开机那两行 `PRESS KEY` / `waiting button` 残留也是同一个原因。

旧版每 80 ms 重画**也治不了**（重画调的同样是 `DrawStatus()`，同样只写第 0/3 行）。
改成"变了才画"之后，视图切换必须把上一帧**完整**盖掉 —— 否则残留会一直停在屏上，
没有后续重画去"碰运气"擦掉它。所以加了：

```cpp
for (uint8_t r = 1; r < ROWS; ++r) {
  if (r != 3u) { Row(r, "", ST7735::BLACK); }   /* 第 3 行下面三个分支必写 */
}
```

### 4) `g_snap[]` 快照移出重画条件

`g_snap[30]`/`g_snap[31]` 那两个"中断进入次数 / 心跳顶事次数"计数器，
是判"中断到底有没有在干活"的**唯一依据**（正常情况 `[31]` 应恒为 0）。
它们要是跟着重画一起变成"几十秒才更新一次"，这套诊断就废了。
快照只有几十字节 RAM 写入 + 一个 `now` 时间戳，成本可忽略，所以改成**每轮都写**。

### 效果（预期）

| 指标 | 改前 | 改后 |
|---|---:|---:|
| 空闲期整屏重画 | 12.5 次/秒 | **0 次/秒**（内容不变时），最坏 0.5 次/秒（看门狗） |
| 空闲期 SPI 占用 | ≈ 25.6 KB × 12.5 = 320 KB/s | **≈ 0**（不变时） |
| 空闲期 RX 心跳间隔 | 99.87 / 100.47 / **121.18** ms（周期 321 ms） | **应稳定在 ~100 ms** |
| 中断块读取间隔 | 19.7 / 19.9 / 40.8 ms（周期 80.4 ms） | **应稳定在 ~20 ms** |
| `Code` | 16648 | **16836**（+188 B） |

### 二进制级验证（`fromelf -c`）

`App_Run` 被完全内联进 `$Super$$main`（`0x08001c0c`，size `0x1ca4`），
`DrawStatus` / `DrawNoPdo` 也被内联进去，所以下面的地址都落在那一段里。

| 检查项 | 证据 |
|---|---|
| 指纹逐字段比对 | `0x08003570`–`0x080035a6`：`LDRB r0,[r1,#0..5]` + `CMP` 共 **6 个字节字段**（`r1` = `&uiFpLast`） |
| `pdo[]` 用循环比 | `0x080035a8`–`0x080035d0`：`LDR r3,[sp+0xe8+off]` / `LDR r4,[sp+0x110+off]` / `CMP` + `ADDS r1,r1,#4`，`SUBS #0x20` 定界 |
| 2 s 看门狗阈值 | `0x080035da`：`LSRS r1,r2,#4` + `CMP r1,#0x7c` → `(now-tUi)>>4 > 124` ⟺ **≥ 2000 ms** |
| 80 ms 限速阈值 | `0x080035ec`：`CMP r2,#0x4f` → `now-tUi > 79` ⟺ **≥ 80 ms** |
| 门控合流 | `0x080035fc`：`CMP r2,#1` + `BEQ 0x08003602`（画）/ `B 0x0800387e`（跳过，直接进第 5 块） |
| `uiFpLast = fp` | `0x08003602`–`0x08003610`：`LDM r0!,{r2-r4}`/`STM r1!,{r2-r4}` ×3 = **36 字节**（6+2 填充+28） |
| `tUi = now` | `0x0800387a`：`LDR r0,[sp,#0xc4]`（now）→ `STR r0,[sp,#0x84]`（tUi） |
| `dirty = false` | `0x08003860` / `0x08003620`：`STR r1,[sp,#0xb8]`（`sp+0xb8` = `dirty`），**只在画图路径上** |
| 空白行循环起始/终止 | `0x080036e2`：`MOVS r5,#1`（r=1）；`0x08003702`：`CMP r5,#7` + `BCS`（r<8） |
| 跳过第 3 行 | `0x08003708`：`CMP r5,#3` + `BEQ 0x08003700` → **r=1,2,4,5,6,7 共 6 行** |
| `Row(0)` 配色 | `0x080036ca`：`g_rowBg = 0x10a2`(C_BAR)、`g_rowFg = 0x07ff`(CYAN=C_KEY)、`g_rowTop = 0` |
| 合并后的 `Row(3)` | `0x0800385c`：`g_rowBg = 0`(BLACK)、`g_rowFg = 0xffe0`(YELLOW)、`g_rowTop = 3*ROW_H`，**三个分支合流到一处** |
| 三个分支确实存在 | `0x08003744` `LDRB r0,[g_diag+8]`(=cc) → `WAIT ATTACH`；`0x0800374c` `LSLS #24`(=srTries==0) → `LISTENING`；否则 `BL __0snprintf`(`SOFT RESET %u/%u`) |
| 第五轮改动未回退 | `BL readBytes` **3**、`BL readReg8` **13**、`BL writeReg` **32** —— 与第五轮完全一致 |

### 逻辑分析仪验收点

1. **空闲期 I²C 心跳恢复成一条直线**：拿到 PDO 之后不再出现 321 ms 的
   "99.87 / 100.47 / 121.18" 周期，应稳定在 ~100 ms（±几毫秒的抖动）。
2. **SPI 在空闲期应该基本安静**：接 PA5/SPI_SCK，拿到 PDO 之后除每 2 秒一次
   的看门狗重画外，不应再有连续脉冲。
3. **中断块间隔恢复成 ~20 ms**：不再出现 80.4 ms 的周期。
4. **功能不回退**：PDO 列表照常显示、`g_rdoAck` 照常置位、0 告警。
5. **拔出时不留残影**：拔线后屏上应只剩 `WAIT ATTACH`，不再看到旧的 PDO 文字。

---

## 6f. ★ 2026-10-09 第七轮：`readBytes()` 每次多读 1 个字节（第五轮引入的回归）

### 现象（业主上报）

屏上显示 **`1 FIX 25.6V 4.00A` / `2 FIX 0.0V` / `3 FIX 0.0V` / `4 PPS …`**，
而源端（SW3526S）实际广播的是 `5V 2A / 9V 3A / 12V 3A / PPS 3.3-11V 3A`。
抓包：PD 侧 `wrongpd.csv`，I²C 侧 `c.csv`（业主口述的 "iiclast"）。

### 根因：`readBytes()` 的收尾时序，每次 burst 都**多时钟 1 个字节**

`c.csv` 里最直接的证据（763.054 ms 那条 Source_Capabilities）：

```
763.054 ms  W44:43 | R45->[E0 A1 41 C8]        ← 请求 3 字节（token+header），实际读了 4 个
763.243 ms  W44:43 | R45->[90 01 08 2C D1 ...] ← 少了 C8，整段错位 1 字节
```

原始波形逐字节看得很清楚（`c.csv` 4834–4846 行）：

```
Start repeat | Address read: 45 | Read | ACK
Data read: E0 | ACK
Data read: A1 | ACK
Data read: 41 | ACK
Data read: C8 | NACK      ← 多出来的第 4 个字节，而且 NACK 落在了它身上
Stop
```

**NACK 落在第 4 个字节上，说明我们要的"最后一个字节"被算错了一位。**

后果有两个，第二个比第一个严重得多：

| # | 后果 | 证据 |
|---|---|---|
| 1 | **FIFO 突发读整段错位** | head 读 3 字节变 4 → FUSB302 的 FIFO 读指针也多走 1 格 → 下一段 burst 从 `90` 开始而不是 `C8` |
| 2 | **状态突发读会偷吃 PD 报文字节** | `W44:3C | R45->…` 请求 7 字节、实际读 8 个；第 8 个正好是 **`0x43 = FIFOS`** → 每读一次状态就白弹掉一个 PD 报文字节 |

第 2 条的抓包铁证：`reg 0x3C read-len = **8**` 出现 **1571 次**。

### 错在哪：v1 型 I²C 的 ACK 位是"事后生效"的

v1 型 I²C 的 ACK 位**不是**在接收前决定这个字节收不收 —— 主设备先把第 k 个字节
收完（ACK=1 时自动 ACK 它），我们才来得及改 ACK 位，而此时第 k+1 个字节已经
开始收了。所以"改成 NACK"永远只能作用在**再下一个**字节上，
**多出来一个字节是必然会被时钟出去的**。

要 NACK 到第 N-1 个（最后一个）字节，必须在
"第 N-3 个在 DR、第 N-2 个在移位寄存器、第 N-1 个刚开始收"这一瞬间把 ACK 拉 0 ——
这个瞬间恰好就是 **BTF=1 且此前只读走 N-3 个字节**。

> 第五轮按 RM0008 Method 1 的字面读法写成了"读 N-2 个"，
> **正好差一个字节**。这个字面读法很容易踩，必须靠抓包数字节数来验。

### 修法

`FUSB302.cpp::readBytes()`，N≥3 的收尾改成：

```cpp
while (remain > 3u) { ... buf[i++] = DR; --remain; }   /* ① 读 N-3 个 */
waitFlag(BTF);                                         /* ② DR=第N-3、SR=第N-2 */
LL_I2C_AcknowledgeNextData(I2Cx, LL_I2C_NACK);         /* ③ 第 N-1 个会被 NACK */
buf[i++] = LL_I2C_ReceiveData8(I2Cx);                  /* ④ 第 N-3 个 */
buf[i++] = LL_I2C_ReceiveData8(I2Cx);                  /* ⑤ 第 N-2 个 */
waitFlag(RXNE);                                        /* ⑥ ★ 等最后一个进 DR */
LL_I2C_GenerateStopCondition(I2Cx);                    /* ⑦ 再挂 STOP */
buf[i++] = LL_I2C_ReceiveData8(I2Cx);                  /* ⑧ 第 N-1 个（已被 NACK） */
```

两处关键改动：**循环上界 2 → 3**，以及 **⑥ 把 STOP 挪到"最后一个字节到齐之后"**
（旧版先挂 STOP 再读，STOP 落在倒数第 2 个字节后面，主设备只能再时钟一个字节出去）。

第 ⑧ 步读的是**被 NACK 的字节**，这在本芯片上是安全的 —— 依据是 N=1 那条路径
（读之前就挂 NACK）返回的值都合理，例如 `STATUS1 (0x41) -> 0x28`
= `RX_EMPTY|TX_EMPTY`，说明被 NACK 的字节照样会进 DR、照样读得出来。

`N=2` 那条分支原来是错的（NACK 和 STOP 都挂在清 ADDR 之前 → 只收到 1 个字节），
一并改成"先等 RXNE 再挂 NACK"的写法。**该分支本工程用不到**
（调用方只有 N=1/3/7/≥4），是按同一套时序模型推出来的，没有抓包佐证。

### 为什么第五轮"验收通过"却漏掉了

第五轮的验收点只数了**事务条数**（"23 笔变 2 笔"、"4 笔变 1 笔"），
**没有数每条事务里的字节数**。多 1 个字节完全不影响事务条数，所以当时看不出来。
→ 以后验收必须**同时数字节数**。

### 效果（预期）

| 指标 | 修前（c.csv 实测） | 修后（预期） |
|---|---|---|
| `0x3C` 起状态突发读的字节数 | **8** | **7** |
| FIFO head 读字节数 | **4** | **3** |
| FIFO body 读字节数 | **21** | **20** |
| 每次状态读偷吃的 PD 字节 | 1 | **0** |
| PDO 列表 | 25.6V/4.00A、0V、0V、PPS | **5.0V/2.00A、9.0V/3.00A、12.0V/3.00A、PPS** |
| `Code` | 16836 | **16872**（+36 B） |

### 二进制级验证（`fromelf -c`，`readBytes` @ `0x08001a90`）

| 检查项 | 证据 |
|---|---|
| 循环上界改成 3 | `0x08001ba8`：`LDR r2,[sp,#0xc]`(remain) + `CMP r2,#4` + `BCC` → 只有 `remain >= 4` 才进循环（= `remain > 3`） |
| 循环体：等 RXNE 再读 | `0x08001bb8`：`LDR r4,[r0,#0x14]`(SR1) + `LSLS r4,r4,#25` + `BMI`（bit6 = RXNE）；`0x08001bca`：`STRB r2,[r3,r7]`（`buf[i] = DR`） |
| 等 BTF | `0x08001bde`：`LDR r2,[r0,#0x14]` + `LSLS r2,r2,#29` + `BMI`（bit2 = BTF） |
| 挂 NACK | `0x08001b7a`：`LSLS r1,r6,#10`（bit10 = ACK）→ `0x08001bf2`：`BICS r2,r2,r1`（清 ACK = NACK） |
| 连读 2 个 | `0x08001bfa`/`0x08001c02`：两次 `LDR r2,[r0,#0x10]`(DR) + `STRB` |
| **等最后一个字节** | `0x08001c08`：`LDR r3,[r0,#0x14]` + `LSLS r3,r3,#25` + `BMI`（**第二次 RXNE 等待**） |
| **再挂 STOP** | `0x08001c1c`：`LSLS r2,r4,#9`（bit9 = STOP）→ `0x08001c20`：`ORRS r3,r3,r2` + `STR` |
| 读最后一个 | `0x08001c24`：`LDR r0,[r0,#0x10]`(DR) + `0x08001c26`：`STRB r0,[r1,#2]` |
| 低层调用次数不变 | `BL readBytes` **3**、`BL readReg8` **13**、`BL writeReg` **32** —— 与第五/六轮一致 |

### 逻辑分析仪验收点（**必须数字节数**）

1. **状态突发读恰好 7 字节**：`W 44:3C | R 45->[xx xx xx xx xx xx xx]` —— 数一下是 7 个。
2. **FIFO head 读恰好 3 字节**：`W 44:43 | R 45->[E0 A1 41]`，第 3 个后面就是 NACK + Stop。
3. **FIFO body 读恰好 20 字节**（4 个 PDO 时）：`90 01 08 …` 这类错位应该消失，
   应能对上 `C8 90 01 08 | 2C D1 02 00 | 2C C1 03 00 | 3C 21 DC C0`。
4. **屏上 PDO 列表与源端广播一致**：`5.0V/2.00A`（含 Unconstrained 标志）、
   `9.0V/3.00A`、`12.0V/3.00A`、`PPS`。
5. **`g_rdoAck` 照常置位**、0 告警、`g_snap[31]`（心跳顶事次数）保持 0。

---

## 6g. ★ 2026-10-09 第八轮：**广播停之后**按按键，Soft Reset 发了也协商不成

### 现象（业主上报）

第七轮修完 `readBytes()` 之后，业主用逻辑分析仪复测，**开机 5 秒内**按按键
（源端正在广播）一切正常。但业主补了一句关键信息：

> 不对，刚刚那是 SW3526S 上电后广播的**五秒内**的过程，这个情况 OK，
> 但是**不再广播之后**，有问题：`pdbad.csv` / `iicbad.csv`

即：**源端广播停了以后再按按键 → 发 Soft Reset → 拿不到 PDO**。

### 先看"广播期内"和"广播停之后"差在哪

这是本轮最重要的一张对照表 —— 两次协商**只差一个时间常数**：

| | 广播期内（`pdnew.csv`） | 广播停之后（`pdbad.csv`） |
|---|---|---|
| 源端什么时候发 caps | **它自己每 ~150ms 一组**（每组 3 条，组内间隔 2ms） | 只有我们发 Soft Reset **催**它才发 |
| 我们发 Soft Reset 后多久收到 caps | 不一定 —— 可能立刻，也可能要等下一组 | **7.5ms**（源端正等着，催一下就回） |
| 我们有多少余量 | ~150ms（下一组还没来，来得及） | **只有 tSenderResponse = 24~30ms** |
| 一次 35ms 的整屏重画 | 无所谓 | **致命** |

**根因就在最后一行**：广播停了之后，"发 Soft Reset → 收 caps → 回 Request"
这条链整个只剩 30ms，而我们的界面刷新一次要 35ms。

### 时间线对齐（两份抓包同一根线，绝对时间可对齐）

`iicbad.csv` 起点 = 2658.117ms，`pdbad.csv` 起点 = 2915.611ms，两者相差
257.494ms —— 正好是 I²C 抓包里 Soft Reset 那一笔的时刻，所以可以直接对齐。

| 绝对时刻 | 来源 | 事件 | 说明 |
|---|---|---|---|
| 2915.611ms | pdbad | `SNK: SOFT RESET` | 我们发出去的 |
| 2915.281ms | iicbad | `W44:43 12 12 12 13 82 4D 00 FF 14 FE A1` | Soft Reset 的 TX FIFO 写入（11B，**字节完全正确**） |
| 2915.6 → 2951.6ms | iicbad | **36.3ms 总线全静默** | ★ 主循环被 35ms 整屏重画堵死 |
| 2916.190ms | pdbad | `SRC: GOOD CRC` | 源端确认收到 |
| 2921.969ms | pdbad | `SRC: ACCEPT` | 源端受理 Soft Reset |
| **2923.117ms** | pdbad | **`SRC: SOURCE CAP`**（4 PDO） | ★ tSenderResponse 起点 |
| 2951.599ms | iicbad | 第一次 FIFO 读 `W44:43 \| R45->[E0 61 01]` | caps 在 FIFO 里**躺了 28.5ms** 才被读走 |
| 2953.550ms | iicbad | `W44:43 12 12 12 13 86 82 12 2C B1 04 10 FF 14 FE A1` | Request(RDO) 写入（**字节完全正确**） |
| **2952.812ms** | pdbad | **`HRST`** | ★ 源端超时 = caps + **29.695ms** |
| 2954.078ms | iicbad | `W44:3C \| R45->[0D 01 01 01 83 20 51]` | 读到 `INTERRUPTA=0x01`(I_HARDRST) + `STATUS0A=0x0D`(HARDRST) |

**我们只差 0.74ms。**

- 源端给我们的窗口：`2952.812 − 2923.117 = 29.695ms`（正好落在规范 24~30ms 内）
- 我们的 Request 落地：`2953.550ms` → **迟到 0.738ms**

也就是说：**只要把那次 35ms 的重画压到 5ms 以内，就赢了。**

### 根因：`dirty = true` 引发的 35ms 整屏重画

发 Soft Reset 时界面要从 `LISTENING...` 变成 `SOFT RESET 1/3`（第 3 行），
于是主循环第 4 块调 `DrawStatus()`。而 `DrawStatus()` 会：

1. `HeaderBar()` —— 160×10 实心填充；
2. 擦第 1/2/4/5/6/7 行 —— 6 × 160×10；
3. 写第 3 行 —— 160×10。

**9 个 160×10 矩形 ≈ 3200B×9 ≈ 28.8KB SPI**。SPI 跑 12MHz，加上
`st7735_send_data()` 每行都要 `st7735_dma_wait()` **阻塞等 TC + BSY**
（见 `LoveFinderLibForPY32_LL/ST7735/st7735.cpp:253`），实测就是 **35~47ms**。

**这 35ms 里我们连 FIFO 都不读**（I²C 抓包上看得清清楚楚：2915.6→2951.6ms
一共 36.3ms 没有任何事务）。源端的 caps 就在这段时间里躺进 FIFO，
等我们醒过来已经过了 28.5ms。

> ⚠️ 注意：真正"顶掉 Request"的**不是**收到 caps 那一轮的重画 ——
> Request 是在第 3 块（RX 服务）里发的，重画在第 4 块，同一轮里
> Request 先走。**顶掉它的是上一轮那次 35ms 重画**，它让第 3 块整整晚了 28ms。

### 第二个触发点：中断块里的 `dirty = true`

第七轮之前的代码在 INTERRUPTA/INTERRUPTB 服务分支里各有一句 `dirty = true`。
但这两个寄存器的内容**一个字都不上屏**，而 `INTERRUPTB.bit0`(I_GCRCSENT)
在**每收到一包**、**每自动回一次 GoodCRC** 时都会置位 —— 于是旧版是
"每收到一包就整屏重画一次 35ms"。这一版一并删掉（保留 `Log()` 和
`intaAcc/intbAcc` 累加器；HARDRST → `recover()` 那条路径不动）。

### 改了什么

| # | 改动 | 位置 |
|---|---|---|
| 1 | **行内容缓存** `g_rcTxt[9][23]` + `g_rcFg[9]` + `g_rcBg[9]` + `g_rcOk[9]`；`Row()` 先比"补齐到 23 列之后的文字 + 前景/背景色"，完全相同就直接 `return`（**一个字节 SPI 都不发**） | `main.cpp` `Row()` / `HeaderBar()` |
| 2 | `HeaderBar()` 同样加缓存（占缓存槽 `ROWS`=8，它没有文字） | `main.cpp:468` |
| 3 | `InvalidateRow(idx)` 让某一行缓存失效 | `main.cpp:348` |
| 4 | **看门狗语义变更**：`PD_UI_WATCHDOG_MS` 2000→**500**，从"每 2s 整屏重画"改成"每 500ms 只作废**一行**（`g_rcWatchRow` 轮转 0..8） | `main.cpp:79` / 主循环第 4 块 |
| 5 | **删掉中断块里的两句 `dirty = true`** | `main.cpp:1432` 附近 |
| 6 | `DrawStatus()` / `DrawPdoList()` 的擦除行照旧调用 —— 缓存会替它们跳过"本来就空白"的行 | 不变 |

**关键性质：一次"状态更新"最多只花 1 行 ≈ 4ms。**
发 Soft Reset 时真正变的只有第 3 行，所以 35ms → **~4ms**，
Request 能在 caps 到达后 ~10ms 内发出去，**余量 ~20ms**。

**看门狗为什么不能"一次全作废"**：整屏 9 行一起作废 = 又变回 35ms 重画，
等于把刚修好的坑挖回来。所以改成轮转单行；代价是整屏自愈一遍要
9×500ms = 4.5s（看门狗本来只兜"SPI/DMA 偶发抽毛刺"，4.5s 完全够）。

### 效果（预期）

| 指标 | 修前（iicbad.csv 实测） | 修后（预期） |
|---|---|---|
| 发 Soft Reset 后的 I²C 静默 | **36.3ms** | **≤ 5ms** |
| caps 从到达到被读走 | 28.5ms | **≤ 5ms** |
| Request 落地时刻（相对 caps） | +30.4ms（**迟到 0.74ms**） | **+8~12ms** |
| 源端 `tSenderResponse` | **超时 → HRST** | 正常收到 Request |
| 协商结果 | 失败，之后源端重播 237 次 caps 全程无应答 | **ACCEPT → PS_RDY → PDO 列表** |
| `Code` | 16872 | **15968**（−904B，`Row()` 改独立函数） |
| `ZI` | 3776 | **4032**（+256B 行缓存） |

### 二进制级验证（`fromelf -c`）

| 检查项 | 证据 |
|---|---|
| `Row()` 变成独立函数 | 符号 `_ZN12_GLOBAL__N_13RowEhPKctt`，size 262，@`0x08000f48`（第七轮是内联进 `$Super$$main` 的，这正是 `Code` 反而变小 904B 的原因） |
| `Row()` 的"r ≥ ROWS 直接返回" | `0x08000f48`：`CMP r0,#7` + 条件返回 |
| `Row()` 读缓存标志 | `LDRB` `g_rcOk[r]` + `CMP #1` |
| `Row()` 比前景/背景色 | 两次 `LDRH` + `CMP` |
| `Row()` 比 23 字节文字 | 循环 `CMP r1,#0x17`（=23）+ 早退 `POP` |
| `Row()` 命中后更新缓存 | `memcpy` 到 `g_rcTxt[r]` + `STRH` 写 `g_rcFg/g_rcBg` + `STRB #1` 写 `g_rcOk` |
| `HeaderBar()` 缓存 | @`0x0800343c`：`LDRB g_rcOk[8]` + `CMP` 背景色 `0x10a2`(=C_BAR) + 命中直接返回 |
| 看门狗轮转 | @`0x080033f0`：`STRB r1,[r2,r0]` 回写 `g_rcWatchRow`；`MULS #0x39 / LSRS #9 / MULS #9 / SUBS` = 对 9 取模 |
| 第 4 块门控 | @`0x080033b8`–`0x080033dc`：`CMP r1,#0x4f`(=79，即 80ms 限速) + 指纹 36 字节拷贝（3× `LDM/STM {r4-r6}`） |
| 中断块 `dirty` 已删 | 原两处 `STRB #1` 到 `dirty` 的指令消失（只剩 HARDRST→`recover()` 那条路径里的） |

### 逻辑分析仪验收点

1. **I²C 心跳稳定**：拿到 PDO 后（或放弃后）空闲期每 ~100ms 一笔
   `W44:40 / W44:41`（`PD_RX_IDLE_MS`），**不再有 35ms 的空白**。
2. **SPI 平时安静**：稳态下 CH0/CH1/CH2 基本不动；每 500ms 只出现
   **一小段 ~4ms** 的刷屏（单行），**不是**原来那种 35ms 长脉冲串。
3. **广播停之后按按键**：`SNK:SOFT RESET → SRC:ACCEPT → SRC:SOURCE CAP →
   SNK:REQUEST(RDO) → SRC:ACCEPT → SRC:PS_RDY`，全程 **~15ms** 走完，
   不再出现 `HRST`。
4. **Request 落地时刻**：相对 `SOURCE CAP` 应 **≤ 15ms**（验收线：25ms）。
5. **源端不再重播**：修好后不该再看到"237 条 SOURCE CAP 一条都不回"那种
   刷屏 —— 只要协商成了，源端就停播了。

### ✅ 验收实测（2026-10-09 16:36，`iicnow.csv` + `pdnow.csv`）—— **一次成功**

业主在**开机 5s 后、确认 CC 上没有广播**的情况下按按键复测，结果：

**PD 侧（`pdnow.csv`，全程只有 12 条报文，无 HRST、无重播）：**

| # | 相对时刻 | 报文 |
|---|---|---|
| 1 | 0.000ms | `SNK: SOFT RESET` |
| 2 | 0.578ms | `SRC: GOOD CRC` |
| 3 | 6.487ms | `SRC: ACCEPT` |
| 4 | 7.051ms | `SNK: GOOD CRC` |
| 5 | **7.635ms** | **`SRC: SOURCE CAP`** — [1] Fixed 5V 3A(15W)[unconstrained] / [2] Fixed 9V 3A(27W) / [3] Fixed 12V 3A(36W) / [4] PPS 3.3/11V 3A(33W) |
| 6 | 8.708ms | `SNK: GOOD CRC` |
| 7 | **10.294ms** | **`SNK: REQUEST`** — PDO #1 Fixed 5V，3A(operating)/3A(max) |
| 8 | 11.020ms | `SRC: GOOD CRC` |
| 9 | 12.457ms | `SRC: ACCEPT` |
| 10 | 13.021ms | `SNK: GOOD CRC` |
| 11 | 64.185ms | `SRC: PS RDY` ← 申请成功 |
| 12 | 64.749ms | `SNK: GOOD CRC` |

**关键数字：`SOURCE CAP → SNK REQUEST` = 2.659ms**（验收线 25ms，**余量 ~27ms**）。

**I²C 侧（`iicnow.csv`，464 笔事务 / 4.655s / 0 告警）：**

| 检查项 | 上一轮（`iicbad.csv`） | 本轮（`iicnow.csv`） |
|---|---|---|
| 发完 Soft Reset 后的总线静默 | **36.3ms** | **5.9ms** ✅ |
| caps 读到 → Request 写出 | 1.95ms | **0.95ms** |
| `FIFOS(0x43)` 写次数 | 2（含失败那次的 Request） | **2**（Soft Reset + Request，**没有多余的**） |
| `RESET(0x0C)` 写次数 | 3 | **1**（只有 init）→ **全程没触发过一次 `recover()`** |
| `INTERRUPTA.bit0`(I_HARDRST) | 置起过 1 次 | **236 次突发读里全是 `0x00`** ✅ |
| 最长总线停顿 | 47.0ms | **35.3ms ×2，且都不在关键路径上** |

那两次 35.3ms 停顿分别是：

1. `rel 1.602→36.889ms` —— **开机首帧**（9 个矩形全画），发生在**按键之前**；
2. `rel 322.943→358.143ms` —— `PS_RDY` 之后切到 PDO 列表视图（`DrawPdoList()`
   要重画全部 8 行），发生在**协商完成之后**，没有 deadline。

其余 462 个间隔全部 < 30ms（中位数 4.9ms，均值 10.1ms），
其中 171 个是 20ms 的中断闩锁轮询（`PD_INT_POLL_MS`）。

**和"广播期内"对照（`pdnew.csv`，改前就正常的那份）：**

| 指标 | 广播期内 | **广播停之后（本轮）** |
|---|---|---|
| `SOURCE CAP → SNK REQUEST` | 2.662ms | **2.659ms** |
| `SRC ACCEPT → SRC PS RDY` | 51.006ms | 51.707ms |

→ **两者行为已经一模一样**。后面那个 51ms 是**源端（SW3526S）自己**切电源的时间，
两次抓包完全一致，不是我们能控的。

**结论：第八轮的行级缓存彻底解决了"广播停之后协商不成"的问题。**

### ⚠️ 本轮**没有**动、但要盯住的一件事：Hard Reset 之后 RX 会锁死

抓包里还有一条独立的现象，本轮**不修**，先记录：

- `pdbad.csv` 一共 237 条 `SOURCE CAP`，但 `SNK: GOOD CRC` **只有 3 条**，
  `SNK: REQUEST` **0 条**。
- 即：源端在 15.6s 里重播了 237 次，我们**几乎一次都没应答**。
- `iicbad.csv` 里能看到原因：从 2954.078ms 读到 `I_HARDRST` 起，
  直到 **3858.0ms** 才第二次 `pd.recover()`（PD_RESET）；
  中间 900ms 里 FIFO 读（`0x43`）**一次都没有** ——
  因为 FUSB302 根本没往 FIFO 里放东西。

这其实就是 `main.cpp:1016` 那条早就写下的已知坑：

> 开机那发 Hard Reset 之后，FUSB302 的 PD 接收逻辑可能锁死
> （实测：HR 之后 `Status1.RX_EMPTY` 恒为 1，一条报文都进不了 RX FIFO）

本轮已经确认：**收到 HRST 后 `recover()` 会被立刻调用**（I_HARDRST 路径，
2954.356ms 就发了 `W44:0C 02`），但**这一次 recover() 没能把 RX 救回来**，
要等到第二次（3858.0ms）才活过来。

**后果**：一次协商超时 = 整场报废（源端会一直重播 + 周期 HRST）。

**判断**：本轮的行缓存改完之后，源端**不应该再发 HRST**（Request 赶得上了），
所以这条路径正常情况下不会被走到。**请业主烧录后重点复测这一点**：
如果广播停之后按按键能一次成功，这条就先搁置；
如果仍偶发 HRST，下一步就该把 HR 恢复做成"recover() 后校验
`Status1.RX_EMPTY`，失败就再来一次 / 或直接重挂 CC"。

> **✅ 16:36 复测结果：没有触发。** `iicnow.csv` 里 `RESET(0x0C)` 只写了 1 次
> （init 那一次），236 次突发读里 `INTERRUPTA` 全是 `0x00` ——
> **整场没收到过一次 Hard Reset，`recover()` 一次都没被调用**。
> 这条先搁置（结论：只要 Request 不迟到，源端就不会发 HRST，
> 也就走不到这个坑里）。

---

## 6g-补. ★ 2026-10-09 第八轮补充：**广播期内**的验收 + 消掉"按键后第一帧 35ms"

### 先看验收：广播期内（`iicnow2.csv` + `pdnow2.csv`）—— **也是一次成功**

业主在**开机 5s 内（源端正在广播）**按按键，结果：

| # | 相对时刻 | 报文 |
|---|---|---|
| — | 0.000ms | `HRST` ← 开机那一枪（我方主动发的，`Control3=0x40`） |
| — | 995.854ms | 源端开始广播（每 ~145ms 一组、每组 3 条，msgId 0→7 循环） |
| 26 | 2189.634ms | `SRC[0]: SOURCE CAP` ← 我们听到的那一组 |
| 27 | 2190.707ms | `SNK[0]: GOOD CRC` |
| 28 | **2192.335ms** | **`SNK[0]: REQUEST`** — PDO #1 Fixed 5V，2A |
| 29 | 2193.057ms | `SRC[0]: GOOD CRC` |
| 30 | 2194.587ms | `SRC[1]: ACCEPT` |
| 31 | 2195.150ms | `SNK[1]: GOOD CRC` |
| 32 | 2246.309ms | `SRC[2]: PS RDY` |
| 33 | 2246.873ms | `SNK[2]: GOOD CRC` |

**`SOURCE CAP → SNK REQUEST` = 2.701ms**（余量 ~27ms）。
**全程没有 `SNK: SOFT RESET`** —— 因为源端本来就在广播，"先听 250ms"
直接听到 caps，**根本不用催**（这正是设计意图）。

I²C 侧（`iicnow2.csv`，344 笔 / 5.048s / 0 告警）：

| 检查项 | 值 | 说明 |
|---|---|---|
| `FIFOS(0x43)` **写**次数 | **1** | 只有那条 Request —— 确实没发 Soft Reset |
| `RESET(0x0C)` 写次数 | 2 | `0x01`(开机 SW_RESET) + `0x02`(按键后 `recover()` 的 PD_RESET) |
| `INTERRUPTA.bit0` | 全 0 | 没有 Hard Reset |
| `Control3(0x09)` | `0x40` @9.308ms | 开机那一枪 Hard Reset |
| caps 读到 → Request 写出 | **0.978ms** | |
| 最长总线停顿 | 35.298ms **×1** | 见下面 —— 这一条就是本节要修的东西 |

### 但这个抓包暴露了一个**潜伏的 ~7% 失败窗口**

`iicnow2.csv` 里那唯一一次 35.298ms 停顿，位置是
`rel 2056.707 → 2092.005ms` —— **正好在按键按下（rel ~2055）之后 1.7ms 开始**。

它是主循环**第一帧**的 `DrawStatus()`：开机只画了第 3/4/6 行
（`ST7735_FillScreen()` 是整屏直灌、**不走 `Row()`**，所以一个缓存槽都没填），
于是第 0/1/2/5/7 行 + 标题条的 `g_rcOk` 全是 0 → 这 9 个矩形**一次全画 = 35.3ms**。

**危险在哪**：这一帧落在"先听 250ms"窗口的**最开头**。设按键时刻 t=0：

| caps 落到线上的时刻 T | 我们读到它的时刻 | Request 发出 | 源端截止 T+30ms | 结果 |
|---|---|---|---|---|
| `[0, 1.5)` | 被 `recover()` 的 `rxFlush()` 冲掉 | — | — | 等下一组 ✅ |
| **`[1.5, 9)`** | **~37ms（被 35ms 挡住）** | **~38ms** | `T+30` | **迟到 → HRST ❌** |
| `[37, 250]` | `T+5` 以内 | `T+6` | `T+30` | 正常 ✅ |

源端每 ~145ms 广播一组（组内 3 条、跨 ~4ms），所以有一组踩进那 6.5ms 窗口的
概率约 **`(6.5+4)/145 ≈ 7%`** —— 表现就是"**偶尔按一次就失败**"。
而按 §6g 的结论，一旦发 HRST，后面还可能撞上 FUSB302 的 RX 锁死。

### 改了什么（`main.cpp`，开机段）

**开机就把整帧铺一遍，把行缓存填满**：

```cpp
ST7735_Init();
ST7735_FillScreen(ST7735::BLACK);
HeaderBar();                                        /* ← 新增：填标题条缓存槽 */
Row(0, "PD PDO DETECT", C_KEY, C_BAR);              /* ← 新增 */
for (uint8_t r = 1u; r < ROWS; ++r) {               /* ← 新增：1/2/5/7 预铺成空白 */
  if ((r != 3u) && (r != 4u) && (r != 6u)) { Row(r, "", ST7735::BLACK); }
}
Row(3, "PD PDO LIST DET", ST7735::CYAN);
Row(4, "PD STARTING..",   ST7735::YELLOW);
Row(6, "",                ST7735::BLACK);
```

**效果**：按键之后第一帧 `DrawStatus()` 里，只剩**第 3、4 行真的变**
（`PD PDO LIST DET` → `LISTENING...`、`PRESS KEY` → 空白），
其余 7 个矩形全部命中缓存直接跳过。

**35.3ms → ~8ms。** 代入上表：迟到条件是 `T < D − 29`，
`D = 8` 时 `T < −21` —— **不可能发生**，这个窗口从根上没了。

代价：开机多烧 ~24ms SPI（在等按键之前，没有任何 deadline）。

### 二进制级验证（`fromelf -c`，`$Super$$main` @`0x08001b9d`）

| 检查项 | 证据 |
|---|---|
| 开机先填标题条 | `0x08002380`–`0x080023b4`：行缓冲填 `0x10A2`(=C_BAR) 字节对、`SUBS r0,r0,#1` 循环 0xA0=160 次、`ST7735_SetAddressWindow(0,0,9,160)`、`CMP r4,#9 / BCC` 走 10 行 → 就是 `HeaderBar()` 的 160×10 实心填充 |
| 预铺 `Row(0,…)` | `0x080023b6` `LDR r3,#0x10a2`(bg=C_BAR) + `LDR r2,#0x7ff`(fg=CYAN) + `0x080023c0` `BL Row` |
| **空行循环跳过 3/4/6** | `0x080023ea` `MOVS r1,#0x58` = `0b0101_1000` → `TST r0,r1` + `BNE` 跳过 bit3/bit4/bit6 → `0x080023d6` `BL Row`（循环体） |
| 循环上界 | `0x080023e0` `CMP r6,#7` + `BCS` 退出 |
| 三行开机提示 | `0x08002402` `BL Row`（row3, `LDR r2,#0x7ff`=CYAN）、`0x0800240e` `BL Row`（row4, `LDR r2,#0xffe0`=YELLOW）、`0x0800241c` `BL Row`（row6） |

### 逻辑分析仪验收点（这次要专门抓"按键后头 40ms"）

1. **按键后不再有 35ms 停顿**：`WaitForButton` 返回（那笔 `W44:0C 02`）
   之后到下一次 I²C 之间应 **≤ 10ms**（原来是 35.3ms）。
2. **开机多一段 SPI**：上电后除了三色自检，还应有 9 个 160×10 矩形
   的连续刷屏（约 35ms），然后才是 `PRESS KEY`。
3. **广播期内按按键**：`SOURCE CAP → SNK REQUEST ≤ 5ms`，且**没有** `SNK: SOFT RESET`。
4. **反复按键 20 次**（每次都让源端处于广播期）应当**每次都成功**，
   不该再出现"偶尔一次 HRST"。

---

## 7. 烧录 / 验证

```powershell
# 编译
& "C:\Soft\Keil_v5\UV4\UV4.exe" -j0 -r `
  "examples_LL\PD_PDO_List_Det\MDK-ARM\PD_PDO_List_Det.uvprojx" -o build.log

# 产物
examples_LL\PD_PDO_List_Det\MDK-ARM\Objects\PD_PDO_List_Det.hex
```

或用仓库根目录的 `expProjWrite.bat`（例程烧录助手，自动编译 + 尝试 UV4 下载）。

**逻辑分析仪接法建议**（确认 MCU 在跑）：

| 通道 | 接点 | 正常波形 |
|---|---|---|
| 0 | PA5 / SPI_SCK | 刷屏时连续时钟脉冲 |
| 1 | PA3 / SPI_MOSI | 刷屏时数据 |
| 2 | PA7 / LCD_CS | 每行一次片选拉低 |
| 3 | PF1 / I2C_SCL | 每 20~50 ms 一串 400 kHz 脉冲 |

只要屏亮起来，CH0/CH1/CH2 就一定有活动。

---

## 8. 排查：烧录后"只有背光、没有画面"

背光是 MCU 驱动的：`ST7735_Init()` 第一行
`LL_GPIO_SetOutputPin(ST7735_EN_Port, ST7735_EN_Pin)`，
而 `Studio_GPIO_Init()` 把 PA12 拉**低**。
所以 **"背光亮" ⇒ MCU 一定执行到了 `ST7735_Init()`**，可以把故障范围缩到它之后。

按这个顺序查：

1. **先看开机三色（§3.0）**。看到三色 = 屏驱通路没问题，去查文字/PD；
   看不到 = 继续往下。
2. **编译选项必须和 `LCD_DMA_Test` 一致**（`Optim 2` + `v6Lto 1`）。
   这是本项目真实踩过的坑：`Optim 4`(-O3) + LTO 关 那版表现就是"只有背光"。
3. **对照法**：`LCD_DMA_Test` 与本工程除 `main.cpp` / `font_config.hpp` /
   3 个 Keil 工程文件外**全部逐字节相同**（已用 MD5 全树比对确认）。
   任何一处出现差异都说明被改过。
4. **DMA**：确认 `Core/SRC/spi.cpp` 里
   `LL_APB1_GRP2_EnableClock(LL_APB1_GRP2_PERIPH_SYSCFG)`
   在 `LL_SYSCFG_SetDMARemap_CH1()` **之前**，且 `LL_DMA_SetPeriphAddress()` 没丢。
   自检失败时库会永久退化为阻塞 SPI，画面照常（只是慢），**不会黑屏** ——
   所以黑屏基本可以排除 DMA。
5. **硬复位**：本板复位键疑似只复位 DAPLink(CH32V203)。
   烧录后如果画面没变，**断电重上电**再试一次。

---

## 9. 相关文档

- `examples_LL/LCD_DMA_Test/README.md` —— 本工程所基于的屏驱 + DMA 例程
- `examples_LL/LCD_Test/README.md` —— 基础显示例程
- `LoveFinderLibForPY32_LL/FUSB302/README.md` —— FUSB302 驱动与寄存器配置
- `LoveFinderLibForPY32_LL/ST7735/` —— 共享 C++ 屏驱库
