# LCD_DMA_Test

PY32F003 + ST7735 彩屏 **DMA 加速** 示例工程：通过 SPI1_TX→DMA1_Channel3 实现高速屏幕刷新，配合双缓冲渲染管线展示丰富的动画效果。

## 特点

- **DMA 传输**：SPI1_TX 走 DMA1_Channel3，每行像素数据由 DMA 驱动 SPI 发送，CPU 无需逐字节轮询
- **双缓冲逐行渲染**：`ST7735_DrawFrame()` 使用乒乓缓冲（2×320 字节），CPU 计算第 N+1 行时，DMA 正在发送第 N 行，计算与传输完全重叠
- **8 大动画**：彩虹渐变、流动波形、棋盘翻转、弹跳球 + 拖尾、图形画廊、滚动字幕、性能测试（DMA vs 阻塞 FPS 对比）
- **图形原语**：点、线、矩形、实/空心圆、圆角矩形、5×7 文字

## 目录结构

```
LCD_DMA_Test/
├── Core/
│   ├── INC/
│   │   ├── dma.h                # DMA 句柄声明
│   │   ├── gpio.h               # LCD 控制引脚
│   │   ├── spi.h                # SPI1 句柄
│   │   └── ...
│   └── SRC/
│       ├── dma.c                # DMA1_Channel3 (SPI1_TX) 初始化
│       ├── spi.c                # SPI1 Init + MSP（含 DMA 链接）
│       ├── main.c               # 动画主循环
│       └── ...
├── Drivers/                     # HAL 驱动库
├── User/ST7735/
│   ├── st7735.c / st7735.h      # DMA 加速版 ST7735 驱动（核心）
│   ├── fonts.c / fonts.h        # 5×7 点阵字库
│   └── st7735_config.h          # 面板参数（旋转/偏移/反色）
├── MDK-ARM/
│   ├── LCD_DMA_Test.uvprojx     # Keil 工程
│   └── startup_py32f003.s
└── README.md
```

## DMA 架构

```
          CPU 渲染                    DMA 传输
    ┌──────────────┐            ┌──────────────────┐
    │  s_fbuf[0]   │ ◄────────  │  SPI1_TX         │
    │  (row N+1)   │  计算与传  │  DMA1_Channel3   │
    ├──────────────┤  输重叠    ├──────────────────┤
    │  s_fbuf[1]   │ ────────►  │  ST7735 PANEL    │
    │  (row N)     │            │  (PA3 MOSI)      │
    └──────────────┘            └──────────────────┘
```

- **API**: `ST7735_DrawFrame( RowRenderFn )` — 用户填充回调，逐行输出 RGB565 到大端序字节
- **宏**: `ST7735_PUT565(dst, i, color)` — 快速将 RGB565 写入行缓冲

## DMA 配置

| 要素 | 配置 |
|------|------|
| DMA 外设 | DMA1_Channel3（SPI1_TX 固定映射） |
| DMA 时钟 | `__HAL_RCC_DMA_CLK_ENABLE()`（AHBENR DMAEN） |
| 方向 | 内存 → 外设（`DMA_MEMORY_TO_PERIPH`） |
| 地址增量 | 外设不变（SPI DR），内存递增 |
| 数据宽度 | 字节对齐（BYTE） |
| IRQ | `DMA1_Channel2_3_IRQHandler` → `HAL_DMA_IRQHandler` |
| 初始化顺序 | `Studio_DMA_Init()` **先于** `Studio_SPI1_Init()` |

## 动画列表

| 动画 | 原理 | 展示要点 |
|------|------|----------|
| 彩虹渐变 | HSV→RGB 逐行，色相沿 (x,y) 流动 | DMA 逐行发彩虹 |
| 流动波形 | 类似波纹的斜向色带 | 快速波形运动 |
| 棋盘翻转 | 8×8 棋盘周期反色 | DMA 快速翻转 |
| 弹跳球 | 15 帧拖尾轨迹 | 圆 + FillCircle |
| 图形画廊 | 圆/矩形/圆角矩形/斜线 | 图形原语展示 |
| 滚动字幕 | 文字从右向左滚动 | 实时字符渲染 |
| 性能测试 | DMA vs 阻塞 1 秒全屏刷新 | FPS 与 ms/f 对比 |

## 接线

与 `LCD_Test` 完全相同。

## 使用

1. 用 Keil MDK 打开 `MDK-ARM/LCD_DMA_Test.uvprojx`，编译并下载；
2. 上电后自动播放动画循环；
3. 每个动画约 4 秒后自动切换。