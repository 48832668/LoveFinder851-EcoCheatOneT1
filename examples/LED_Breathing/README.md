# LED_Breathing

PY32F003 板载 LED 呼吸灯示例工程：上电后 PA2 上的 LED 按正弦曲线平滑呼吸。

## 说明

- 主控：PY32F003F18U6-E（与 LoveFinder491_PowerOneT2 工程同一块开发板）
- LED：板载 LED，连接至 PA2 / TIM3_CH1（LED_PWM）
- 本工程由 PY32F003 基础工程（LoveFinder851-EcoCheatOneT1）复制而来，
  保留了基础工程 HAL 代码（RCC、GPIO、TIM3），实现了 TIM3 PWM 呼吸灯效果。

## 目录结构

```
LED_Breathing/
|-- Core/                  # PyStudio 生成的 HAL 配置代码
|-- Drivers/               # PY32F003 HAL 驱动 / CMSIS
|-- MDK-ARM/               # Keil MDK-ARM 工程（LED_Breathing.uvprojx）
`-- LED_Breathing.pysprj   # PyStudio 工程文件（含引脚定义）
```

## 原理

1. TIM3 配置为 PWM 输出模式，通道 1 映射至 PA2（LED_PWM）；
2. 主循环使用 64 点正弦查找表，正向（0→1000）和反向（1000→0）连续
   修改 TIM3_CH1 比较值，实现 LED 亮度平滑变化；
3. 单次呼吸周期 ≈ 64 × 15 ms × 2 ≈ 2 秒，节奏接近自然呼吸。

## 使用

1. 用 Keil MDK 打开 `MDK-ARM/LED_Breathing.uvprojx`，编译并下载；
2. 或使用 PyStudio 打开 `LED_Breathing.pysprj` 后生成/编译。

## 参数调整

在 `Core/SRC/main.c` 的 `USER CODE BEGIN PD` 区段可调整：
- `BREATH_STEPS` — 呼吸表步数（默认 64）
- `BREATH_DELAY_MS` — 每步延时（默认 15 ms）

`breath_table` 的峰值 1000 需与 `Core/SRC/tim.c` 中 TIM3 的 ARR（`Init.Period = 1000`）
保持一致，否则亮度无法到达满量程。

## 代码入口

`Core/SRC/main.c` 中 `USER CODE 2`：

```c
HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
```

呼吸逻辑在 `USER CODE BEGIN WHILE`：

```c
while (1)
{
  for (uint8_t i = 0; i < BREATH_STEPS; i++)
  {
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, breath_table[i]);
    HAL_Delay(BREATH_DELAY_MS);
  }
  for (int8_t i = (BREATH_STEPS - 1); i >= 0; i--)
  {
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, breath_table[i]);
    HAL_Delay(BREATH_DELAY_MS);
  }
}
```

## 引脚说明

| 引脚 | 功能    | 说明              |
|------|---------|-------------------|
| PA2  | TIM3_CH1| LED_PWM（呼吸灯） |
| PA13 | SWDIO   | 调试              |
| PA14 | SWCLK   | 调试              |