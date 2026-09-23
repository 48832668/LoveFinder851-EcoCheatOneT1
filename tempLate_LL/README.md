# tempLate_LL —— LL 空工程母版（仅 `main` + 自定义库为 C++）

PY32F003（Puya 普冉）的 **LL 库空工程母版**。新建工程时由它复制而来，只负责
**正确的 IO / 外设初始化**，不含业务逻辑。

- **主控**：PY32F003F18U6-E（Cortex-M0+）
- **驱动库**：LL（`USE_FULL_LL_DRIVER`）
- **编译器**：Arm Compiler 6（AC6 / ARMCLANG，实测 V6.24）
- **语言划分**：**只有 `main` 是 C++17；外设与厂商文件一律 C**
- **SPI 必须启用 DMA**（见下方补丁 2）

> ⚠️ 命名不统一是历史遗留：目录名 `tempLate_LL` / `.pysprj` 名 `EmptyProj_LL` /
> 工程文件 `EmptyProj_LL.uvprojx`。

---

## 1. 语言划分（本模板的核心约定）

| 范围 | 语言 | 文件 |
|---|---|---|
| `main` | **C++17** | `Core/INC/main.hpp`、`Core/SRC/main.cpp` |
| 外设（自有） | C | `Core/{INC,SRC}/` 下的 `crc/dma/gpio/i2c/py32f003_it/rcc/spi/tim/usart`（`.h`/`.c`） |
| 厂商 | C | `Drivers/**`、`system_py32f003.c`、`startup_py32f003xx.s` |

> 业主明确：**"C 改 C++"只针对 `main` 和用户的自定义库文件**，外设文件保持 C。
> 这样 PyStudio 重新生成时不会产生 `.c`/`.cpp` 重复文件。

### `main.hpp` 必须 C/C++ 双兼容

外设 `.h` 全都 `#include "main.hpp"`，而外设是 **C** 文件，所以 `main.hpp` 必须能被 C 编译：

```cpp
#ifdef __cplusplus
#include <cstdint>
#endif

extern "C" {
#ifdef __cplusplus
extern volatile std::uint32_t uwTick;
std::uint32_t BSP_GetTick() noexcept;
#else
extern volatile uint32_t uwTick;
#endif
}
```

- `#include <cstdint>` 放进 `#ifdef __cplusplus`。
- `uwTick` 用纯 C 类型声明（`py32f003_it.c` 是 C，要在 `SysTick_Handler()` 里自增）。
- `BSP_GetTick() noexcept` 这类 C++ 语法放进 `#ifdef __cplusplus`。
- 整体 `extern "C" { }` 包裹，保证 C/C++ 链接一致。

---

## 2. PyStudio 重新生成后必须手工补回的 6 处

前 3 处是**补丁**（都在 `USER CODE` 保护区内），后 3 处是**清理 PyStudio 生成的
C 版 main 残留**（本模板的设计是「只有 `main` 是 C++」，见 §1）。

| # | 文件 / 区段 | 要做的 | 原因 |
|---|---|---|---|
| 1 | `Core/SRC/rcc.c` `Studio_RCC_Init 1` | 补 `NVIC_SetPriority(SysTick_IRQn, (1UL<<__NVIC_PRIO_BITS)-1UL)` + `SysTick->CTRL \|= SysTick_CTRL_TICKINT_Msk` | Puya 的 `LL_InitTick()` 漏置位 TICKINT（ST 原版有）；不补则 SysTick 中断不开、`uwTick`/`BSP_GetTick()` 恒为 0 |
| 2 | `Core/SRC/spi.c` `Studio_SPI1_Init 1` | 补 `LL_DMA_SetPeriphAddress(DMA1, CH1, (uint32_t)&SPI1->DR)` + `LL_SPI_EnableDMAReq_TX(SPI1)` + `LL_SPI_Enable(SPI1)` | PyStudio 的 SPI/DMA 模板永不生成这 3 行；漏 CPAR 会总线挂死 |
| 3 | `Core/SRC/py32f003_it.c` `SysTick_Handler 0` | 补 `uwTick++` | `LL_mDelay()` 不维护毫秒计数器 |
| 4 | `Core/SRC/main.c`、`Core/INC/main.h` | **删除**（PyStudio 每次都会重新生成） | 与本模板的 `main.cpp`/`main.hpp` 重复：两个 `int main(void)` + 两个 `Error_Handler()`，链接必冲突 |
| 5 | `Core/INC/{crc,dma,gpio,i2c,py32f003_it,rcc,spi,tim,usart}.h` | 把 `#include "main.h"` **改回** `#include "main.hpp"` | PyStudio 生成的 `main.h` 里没有 `uwTick`/`BSP_GetTick` 声明，C 文件（尤其 `py32f003_it.c` 的 `uwTick++`）会 `use of undeclared identifier 'uwTick'` 编译失败 |
| 6 | `MDK-ARM/EmptyProj_LL.uvprojx` | 删掉 `Application/User/Core` 组里的 `main.c` 条目 | PyStudio 会把 `main.c` 加进编译列表，即使文件删了也会报找不到源文件 |

> ⚠️ **PyStudio 不保留任何用户代码**。已在 `%APPDATA%\py32studio\data\vendor\Puya\chip-template\`
> 的 `generator\keil.js`、`core\run\v2.cjs` 及整个模板树核实：搜 `preserve` /
> `mergeUserCode` / JS 里的 `USER CODE BEGIN` **全部 0 命中** —— `USER CODE BEGIN`
> 只是 `.hbs` 里的字面文本。**重新生成后上面这些都会消失，必须手工补回。**
> 同理 `keil.js` 写死 `uAC6:"0"`、`v6LangP:"1"`、`MiscControls:""`，**默认吐 AC5**。

> 📌 **重新导出后的自检**：`Keil` 里 clean rebuild 一次，应当得到
> `Code=3508 RO-data=352 RW-data=196 ZI-data=1036`、`0 Error(s), 0 Warning(s)`（见 §4）。
> 数字对不上或报错，就按上表逐条核对。

> 📌 **工程名改不了**：PyStudio 用 `.pysprj` 里的 `"name"` 字段当工程名，
> 界面上改不了 —— 所以本模板的 `tempLate_LL.pysprj` 里是 `"name": "EmptyProj_LL"`，
> Keil 工程也就一直叫 `EmptyProj_LL.uvprojx`（目录名却是 `tempLate_LL`，见文首说明）。
>
> **例程不再带 `.pysprj`**（`examples_LL/*` 已全部删除）：例程是手工维护的 Keil 工程，
> 工程名直接改 `uvprojx` 的 `<TargetName>` / `<OutputName>` 两处，再把文件改名即可
> （`LCD_Test` / `Button_Test` 就是这么来的）。只有本母版保留 PyStudio 工程文件，
> 供新建工程时复制。

---

## 3. Keil 工程设置

| 位置 | 值 | 说明 |
|---|---|---|
| `<uAC6>` | `1` | 使用 AC6（本机只装 ARMCLANG，无 AC5） |
| 每个 `.cpp` 的 `<FileType>` | **8** | 1 = C，**8 = C++**（仅 `main.cpp`） |
| `<v6Lang>` | `5` | C11（厂商 `.c` 仍按 C 编译，保持不动） |
| `<v6LangP>` | **9** | C++ 标准 = `-std=gnu++17` |
| `<MiscControls>` | `-Wno-register` | 厂商 LL 头用了 C++17 已删除的 `register` |

> 厂商头文件的 `register` **不要去改厂商源码**，用 `-Wno-register` 降级即可。

---

## 4. 编译验证（AC6 V6.24，clean rebuild）

```
Program Size: Code=3508 RO-data=352 RW-data=196 ZI-data=1036
0 Error(s), 0 Warning(s)
```

| | Flash 合计 (Code+RO+RW) | RAM 合计 (RW+ZI) |
|---|---|---|
| 本模板 | 4056 B | 1232 B |

> MicroLIB 的 "does not support C++" 警告是厂商默认配置，可忽略（不引用 libc++）。

---

## 5. 目录结构

```
tempLate_LL/
├── Core/
│   ├── INC/  main.hpp  crc.h dma.h gpio.h i2c.h py32f003_it.h rcc.h spi.h tim.h usart.h
│   └── SRC/  main.cpp  crc.c dma.c gpio.c i2c.c py32f003_it.c rcc.c spi.c tim.c usart.c
│             system_py32f003.c（厂商 CMSIS，保持 C）
├── Drivers/
│   ├── CMSIS/
│   └── PY32F003_LL_Driver/
├── MDK-ARM/
│   ├── EmptyProj_LL.uvprojx
│   └── startup_py32f003xx.s
└── tempLate_LL.pysprj
```

---

## 6. 使用

1. 复制本目录为新的工程目录，重命名；
2. 用 Keil 打开 `MDK-ARM/EmptyProj_LL.uvprojx`，按 §3 核对编译器设置，编译下载；
3. 或双击仓库根目录的 `expProjWrite.bat`（脚本扫描
   `examples_LL\<例程>\MDK-ARM\*.uvprojx`，**本模板不在扫描范围内**，仅供例程使用）。

---

## 7. 相关工程

| 工程 | 说明 |
|------|------|
| `examples_LL/LED_Breathing/` | 第一个 C++17 转换样板，坑的完整说明在这里 |
| `examples_LL/LCD_Test/` | LCD 显示例程（全 C++17） |
| `examples_LL/LCD_DMA_Test/` | LCD + SPI1_TX → DMA1_Channel1 加速例程（全 C++17） |
| `examples_LL/Button_Test/` | 按键（EXTI）+ 屏：单击/双击/长按统计，长按阈值可调（全 C++17） |
| `LoveFinderLibForPY32_LL/` | 共享库：屏驱 + 按键 + 字库（在父目录 `C:\Debug\Self\`） |
