# LED_Breathing

PY32F003 板载 LED 呼吸灯示例（**LL 库 + C++17**）：上电后 PA2 上的 LED 按正弦曲线平滑呼吸。

> 本工程是 `examples/LL/` 下第一个完成 **C++17 改造**的工程。
> HAL 版对照在 `examples/HAL/LED_Breathing/`。

## C++17 改造说明

### 文件命名

| 旧（C） | 新（C++17） |
|---------|------------|
| `Core/INC/main.h` | `Core/INC/main.hpp` |
| `Core/INC/gpio.h` | `Core/INC/gpio.hpp` |
| `Core/INC/rcc.h` | `Core/INC/rcc.hpp` |
| `Core/INC/tim.h` | `Core/INC/tim.hpp` |
| `Core/INC/py32f003_it.h` | `Core/INC/py32f003_it.hpp` |
| `Core/SRC/main.c` | `Core/SRC/main.cpp` |
| `Core/SRC/gpio.c` | `Core/SRC/gpio.cpp` |
| `Core/SRC/rcc.c` | `Core/SRC/rcc.cpp` |
| `Core/SRC/tim.c` | `Core/SRC/tim.cpp` |
| `Core/SRC/py32f003_it.c` | `Core/SRC/py32f003_it.cpp` |
| `Core/SRC/system_py32f003.c` | **保持 `.c`**（厂商 CMSIS 文件） |

厂商文件（`Drivers/PY32F003_LL_Driver/`、`Drivers/CMSIS/`、`system_py32f003.c`、
`startup_py32f003xx.s`）**保持 C 编译**，只把我们自己写的代码转 C++17。
这样升级厂商驱动库时不会产生冲突。

### Keil 工程设置（两处，必须改）

| 位置 | 值 | 说明 |
|------|----|----|
| 每个 `.cpp` 的 `<FileType>` | **8** | 1 = C，**8 = C++**。填 1 会当 C 编译 |
| `<v6LangP>` | **9** | C++ 语言标准。9 = `-std=gnu++17`（已用 `.__i` 响应文件实测确认） |

`<v6Lang>`（5 = C11）保持不动，因为厂商 `.c` 文件仍按 C 编译。

### 三个必须知道的坑

**1. 厂商 LL 头文件用了 `register` 关键字，C++17 已把它删掉**

```
error: ISO C++17 does not allow 'register' storage class specifier [-Wregister]
```

`py32f0xx_ll_tim.h` 等头文件里有 `register uint8_t iChannel = ...;`。
`register` 在 C++11 被弃用、**C++17 正式删除**。
解决办法是在工程 C/C++ 选项的 **Misc Controls 里加 `-Wno-register`**
（降级为可接受扩展），**不要去改厂商头文件**：

```xml
<MiscControls>-Wno-register</MiscControls>
```

**2. 中断处理函数必须 `extern "C"`**

启动文件 `startup_py32f003xx.s` 用 `IMPORT SysTick_Handler` 这类**汇编符号按名字**
引用中断入口。C++ 默认做名字修饰，不加 `extern "C"` 会导出成
`_Z15SysTick_Handlerv` 之类，汇编端找不到 → 链接报 undefined symbol，
或者向量表里是空指针。本工程已在 `py32f003_it.cpp` 里用
`extern "C" { ... }` 把全部 ISR 包起来。

> 这是硬件 ABI 层面的要求，跟"要不要兼容 C"无关 —— 只影响这几个入口符号。

**3. `main()` 与 `$Sub$$main` 不受影响**

C++ 标准规定 `main` 不参与名字修饰，`system_py32f003.c`（C 编译）里的
`$Sub$$main` / `$Super$$main` 机制照常工作，SRAM 向量表迁移不受影响。

### 改造结果

| 版本 | Code | RO-data | RW-data | ZI-data | 编译 |
|------|------|---------|---------|---------|------|
| C（改造前） | 1070 | 354 | 196 | 524 | 0 Error / 0 Warning |
| **C++17（现在）** | **1070** | 354 | 196 | 524 | 0 Error / 0 Warning |

**体积完全一致**；向量表 6 个入口地址逐字节相同；map 里确认无 `_Z...Handler`
修饰符号。

## 编译结果（Keil MDK 5.43a / Arm Compiler 6.24，`-O1` + LTO，clean rebuild）

| 版本 | Code | RO-data | RW-data | ZI-data | Flash 合计 | RAM 合计 |
|------|------|---------|---------|---------|-----------|---------|
| HAL `LED_Breathing` | 4092 | 436 | 204 | 588 | 4732 | 792 |
| **LL `LED_Breathing`** | **1070** | 354 | 196 | 524 | **1620** | **720** |
| **差值** | −3022 | −82 | −8 | −64 | **−3112 (−65.8%)** | **−72 (−9.1%)** |

两个工程的外设集完全相同（RCC + GPIO + TIM3，无 USART），因此数字可直接对比。

> HAL 工程为 `<Optim>2</Optim>`（`-O1`）、LTO 关闭；本工程为 `-O1` + LTO，
> 并给 `system_py32f003.c` 单独加了 `-fno-lto`（原因见下方"坑"第 3 条）。
> 若本工程关掉 LTO，Code = 1194。

## 目录结构

```
LED_Breathing/
├── Core/
│   ├── INC/
│   │   ├── main.hpp
│   │   ├── gpio.hpp          # PA0/PA1/PA2/PB5 定义
│   │   ├── rcc.hpp
│   │   ├── tim.hpp           # LED_PWM_Pin / LED_PWM_ARR
│   │   └── py32f003_it.hpp
│   └── SRC/
│       ├── main.cpp          # 呼吸主循环 + uwTick
│       ├── rcc.cpp           # Studio_RCC_Init()  HSI 8MHz + 1ms SysTick
│       ├── gpio.cpp          # Studio_GPIO_Init()
│       ├── tim.cpp           # Studio_TIM3_Init()  TIM3_CH1 PWM
│       ├── py32f003_it.cpp   # ISR（extern "C" 包裹）
│       └── system_py32f003.c # 厂商 CMSIS 文件，保持 C
├── Drivers/
│   ├── CMSIS/
│   └── PY32F003_LL_Driver/   # 纯 LL 库，无 HAL
├── MDK-ARM/
│   ├── LED_Breathing.uvprojx
│   └── startup_py32f003xx.s
├── LED_Breathing.pysprj   # PyStudio 工程（RCC/GPIO/NVIC/TIM3 firmware = ll）
└── README.md
```

## 引脚（与 HAL 版一致）

| 引脚 | 功能 | 配置 |
|------|------|------|
| PA2  | LED_PWM | TIM3_CH1，**AF13**，推挽复用输出 |
| PA0  | FUSB_INT | 模拟输入（本示例未用） |
| PA1  | KEY_INT  | 模拟输入（本示例未用） |
| PB5  | CLK_INT  | 推挽输出 |
| PA13 / PA14 | SWDIO / SWCLK | 调试 |

## 使用

1. 用 Keil MDK 打开 `MDK-ARM/LED_Breathing.uvprojx`，编译并下载；
2. 或双击仓库根目录的 `expProjWrite.bat`（**注意**：该脚本目前只扫描
   `examples\*\MDK-ARM\*.uvprojx`，本工程符合条件，会自动出现在菜单里）。

## HAL → LL 逐行对照

| HAL 版（`tim.c` / `main.c`） | LL 版 |
|---|---|
| `HAL_TIM_Base_Init()` | `LL_TIM_SetPrescaler/SetCounterMode/SetAutoReload/SetClockDivision/SetRepetitionCounter/DisableARRPreload` |
| `HAL_TIM_ConfigClockSource(INTERNAL)` | （LL 无对应动作：内部时钟就是默认态，`SMCR` 保持 0） |
| `HAL_TIM_PWM_Init()` | （LL 无对应动作：`CCMR` 由下面的 OC 系列直接配置） |
| `HAL_TIMEx_MasterConfigSynchronization()` | `LL_TIM_SetTriggerOutput(TRGO_RESET)` + `LL_TIM_DisableMasterSlaveMode()` |
| `HAL_TIM_PWM_ConfigChannel()` | `LL_TIM_OC_SetMode/SetCompareCH1/SetPolarity/DisableFast` |
| `__HAL_TIM_ENABLE_OCxPRELOAD()` | `LL_TIM_OC_EnablePreload()` |
| `HAL_TIM_PWM_Start(&htim3, CH1)` | `LL_TIM_EnableCounter(TIM3)` |
| `__HAL_TIM_SET_COMPARE(&htim3, CH1, v)` | `LL_TIM_OC_SetCompareCH1(TIM3, v)` |
| `HAL_Delay(ms)` | `LL_mDelay(ms)` |
| `HAL_Init()` | `LL_APB1_GRP2_EnableClock(SYSCFG)` + `LL_APB1_GRP1_EnableClock(PWR)` + `LL_PWR_EnableBkUpAccess()` |
| `HAL_TIM_Base_MspInit()`（在 `tim.c` 里） | 直接内联进 `Studio_TIM3_Init()`，无 MSP 文件 |

## 迁移中发现的三个坑（本工程已规避）

### 1. PA2 上 TIM3_CH1 必须用 `LL_GPIO_AF13_TIM3`
LL 头里 `LL_GPIO_AF1_TIM3` 也存在，**用错照样能编译通过，但 PWM 完全没有输出**。
HAL 工程生成的是 `GPIO_AF13_TIM3`，迁移时以它为准。

### 2. `LL_mDelay()` 没有毫秒计数器
PY32 的 `LL_mDelay()` 只轮询 `SysTick->CTRL.COUNTFLAG`，既不维护计数器，
也没有 `LL_GetTick()`。本工程在 `main.c` 里用 `uwTick` + `BSP_GetTick()`
补上了这个能力（`SysTick_Handler()` 里自增），供需要超时/计时的代码使用。

### 3. 开启 LTO 会悄悄废掉 SRAM 向量表 —— 必须按文件关掉
`system_py32f003.c` 会把 Flash 里的向量表拷到 SRAM 再把 `SCB->VTOR` 指过去。
开启 `-flto` 后，编译器发现 `VECT_SRAM_TAB` 只写不读，**把拷贝循环和数组整个删掉**；
而 `SCB->VTOR = SRAM_BASE` 是 volatile 写删不掉 —— 结果 VTOR 指向一片空 SRAM。

平时看不出来，因为 **PY32 的 `LL_Init1msTick()` 没有置位 `SysTick_CTRL_TICKINT_Msk`**
（ST 原版是带的，Puya 这份漏了），SysTick 中断默认根本没开，所以没有中断去跳飞。
但这是个定时炸弹：一旦用到任何中断（本工程已在 `Studio_RCC_Init()` 里补上
`SysTick->CTRL |= SysTick_CTRL_TICKINT_Msk` 让 `uwTick` 能走），就会立刻出事。

**解决办法：只对 `system_py32f003.c` 这一个文件加 `-fno-lto`**（Keil 里右键该文件
→ Options for File → C/C++ → Misc Controls 填 `-fno-lto`），厂商源码一个字不改：

| 配置 | Code | 向量表 | uwTick |
|------|------|--------|--------|
| 无 LTO | 1194 | ✅ 保留 | ✅ 正常 |
| LTO | 1010 | ❌ **丢失** | ❌ 恒为 0 |
| LTO + 本文件 `-fno-lto`（本工程） | **1070** | ✅ 保留 | ✅ 正常 |

> 不要试图给 `VECT_SRAM_TAB` 加 `volatile`/`used` 来强制保留：
> 那样该段会落进 `lto-llvm-*.o`，链接器无法自动放置，报 **L6985E**，
> 还得额外写 scatter 文件。

### 4. 本工程的 `tim.c` 仍是逐个 setter 写法（有意保留）

```c
/* 本工程（已验证上板正常，故未改动） */
LL_TIM_SetPrescaler(TIM3, 0);
LL_TIM_SetCounterMode(TIM3, LL_TIM_COUNTERMODE_UP);
LL_TIM_SetAutoReload(TIM3, LED_PWM_ARR);
...
LL_TIM_OC_SetMode(TIM3, LL_TIM_CHANNEL_CH1, LL_TIM_OCMODE_PWM1);
```

**后续移植请改用整体初始化**，与 `LCD_Test_LL` / `LoveFinder851-EcoCheatOneT1_LL`
保持一致（也是 PyStudio 官方 LL 模板的写法）：

```c
LL_TIM_InitTypeDef    TIM_InitStruct    = {0};
LL_TIM_OC_InitTypeDef TIM_OC_InitStruct = {0};

TIM_InitStruct.Prescaler         = 0;
TIM_InitStruct.CounterMode       = LL_TIM_COUNTERMODE_UP;
TIM_InitStruct.Autoreload        = LED_PWM_ARR;
TIM_InitStruct.ClockDivision     = LL_TIM_CLOCKDIVISION_DIV1;
TIM_InitStruct.RepetitionCounter = 0;
LL_TIM_Init(TIM3, &TIM_InitStruct);
LL_TIM_DisableARRPreload(TIM3);
LL_TIM_SetTriggerOutput(TIM3, LL_TIM_TRGO_RESET);
LL_TIM_DisableMasterSlaveMode(TIM3);

TIM_OC_InitStruct.OCMode       = LL_TIM_OCMODE_PWM1;
TIM_OC_InitStruct.OCState      = LL_TIM_OCSTATE_DISABLE;
TIM_OC_InitStruct.OCNState     = LL_TIM_OCSTATE_DISABLE;
TIM_OC_InitStruct.CompareValue = 0;
TIM_OC_InitStruct.OCPolarity   = LL_TIM_OCPOLARITY_HIGH;
TIM_OC_InitStruct.OCNPolarity  = LL_TIM_OCNPOLARITY_HIGH;   /* 实际宏名见 LL 头 */
TIM_OC_InitStruct.OCIdleState  = LL_TIM_OCIDLESTATE_LOW;
TIM_OC_InitStruct.OCNIdleState = LL_TIM_OCIDLESTATE_LOW;
LL_TIM_OC_Init(TIM3, LL_TIM_CHANNEL_CH1, &TIM_OC_InitStruct);
LL_TIM_OC_DisableFast(TIM3, LL_TIM_CHANNEL_CH1);
LL_TIM_CC_EnableChannel(TIM3, LL_TIM_CHANNEL_CH1);
```

**为什么强调这件事**：`LCD_Test_LL` 就是因为手写逐个 setter 漏掉了
`LL_SPI_Init()` 里的 `FRXTH` 位，导致屏幕雪花/花屏/卡死，排查了两轮。
详见 `examples/LCD_Test_LL/README.md`。

## 参数调整

与 HAL 版相同，在 `Core/SRC/main.c` 的 `USER CODE BEGIN PD` 区段：

- `BREATH_STEPS` — 呼吸表步数（默认 64）
- `BREATH_DELAY_MS` — 每步延时（默认 15 ms）

呼吸表峰值 1000 需与 `Core/INC/tim.h` 的 `LED_PWM_ARR`（默认 1000，
写入 TIM3 的 ARR）保持一致，否则亮度无法到达满量程。

## 相关工程

| 工程 | 说明 |
|------|------|
| `examples/HAL/LED_Breathing/` | HAL 版（保留对照） |
| `EmptyProj_LL/` | LL 空工程母版（本工程由它复制而来） |
