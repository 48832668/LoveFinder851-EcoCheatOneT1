/**
 * @file st7735.c
 * @brief ST7735 160x80 (0.96 inch) LCD driver - DMA accelerated version
 *
 * 相比 examples/LCD_Test 的阻塞版驱动，本版本：
 *   - 像素数据走 SPI1_TX -> DMA1_Channel3 的 DMA 通道；
 *   - 提供双缓冲逐行渲染 ST7735_DrawFrame()，让 CPU 渲染与 SPI 传输重叠；
 *   - 附带常用图形原语（点/线/矩形/圆/圆角矩形）。
 *
 * Init sequence 取自 LoveFinder491_PowerOneT2 LoveFinderLib/ST7735
 * （Panel A: BGR, offsets 0/24, no inversion, DEG_0 landscape 160x80）。
 */

#include "st7735.h"
#include "fonts.h"
#include "spi.h"   /* hspi1 */
#include "dma.h"   /* hdma_spi1_tx (SPI1_TX) */
#include "gpio.h"  /* LCD_DC / LCD_RESET / LCD_CS / LCD_EN */

/*===========================================================================
 * 双缓冲（乒乓）行缓冲：每行 ST7735_WIDTH*2 字节
 *===========================================================================*/
static uint8_t s_fbuf[2][ST7735_WIDTH * 2];
static volatile uint8_t s_dmaDone = 1;   /* 1 = DMA 空闲 */

/* 整数平方根（位运算法，避免引入 math.h / 浮点库） */
static int16_t ST7735_ISqrt(uint32_t n)
{
    uint32_t res = 0U;
    uint32_t bit = 1UL << 30;
    while (bit > n) bit >>= 2;
    while (bit)
    {
        uint32_t tmp = res + bit;
        if (n >= tmp) { n -= tmp; res = (res >> 1) + bit; }
        else          { res >>= 1; }
        bit >>= 2;
    }
    return (int16_t)res;
}

/*===========================================================================
 * Low level helpers
 *===========================================================================*/

static void ST7735_Select(void)
{
    HAL_GPIO_WritePin(LCD_CS_Port, LCD_CS_Pin, GPIO_PIN_RESET);
}

static void ST7735_Deselect(void)
{
    HAL_GPIO_WritePin(LCD_CS_Port, LCD_CS_Pin, GPIO_PIN_SET);
}

static void ST7735_WriteCmd(uint8_t cmd)
{
    HAL_GPIO_WritePin(LCD_DC_Port, LCD_DC_Pin, GPIO_PIN_RESET);
    HAL_SPI_Transmit(&hspi1, &cmd, 1, HAL_MAX_DELAY);
}

static void ST7735_WriteData(uint8_t data)
{
    HAL_GPIO_WritePin(LCD_DC_Port, LCD_DC_Pin, GPIO_PIN_SET);
    HAL_SPI_Transmit(&hspi1, &data, 1, HAL_MAX_DELAY);
}

static void ST7735_WriteDataBuf(const uint8_t *buf, uint16_t len)
{
    HAL_GPIO_WritePin(LCD_DC_Port, LCD_DC_Pin, GPIO_PIN_SET);
    HAL_SPI_Transmit(&hspi1, (uint8_t *)buf, len, HAL_MAX_DELAY);
}

/*--- DMA 传输 ------------------------------------------------------------
 * 启动一次 SPI1 TX DMA（非阻塞）。完成时会进入 HAL_SPI_TxCpltCallback
 * （本文件末尾覆写了该弱回调），从而清除 s_dmaDone。
 *-----------------------------------------------------------------------*/
static void ST7735_DMA_Start(const uint8_t *buf, uint16_t len)
{
    HAL_GPIO_WritePin(LCD_DC_Port, LCD_DC_Pin, GPIO_PIN_SET); /* data mode */
    s_dmaDone = 0;
    if (HAL_SPI_Transmit_DMA(&hspi1, (uint8_t *)buf, len) != HAL_OK)
    {
        /* DMA 启动失败时退化为阻塞发送，避免死等 */
        s_dmaDone = 1;
        HAL_SPI_Transmit(&hspi1, (uint8_t *)buf, len, HAL_MAX_DELAY);
    }
}

static void ST7735_DMA_Wait(void)
{
    while (s_dmaDone == 0)
    {
    }
}

static void ST7735_Reset(void)
{
    HAL_GPIO_WritePin(LCD_RESET_Port, LCD_RESET_Pin, GPIO_PIN_RESET);
    HAL_Delay(20);
    HAL_GPIO_WritePin(LCD_RESET_Port, LCD_RESET_Pin, GPIO_PIN_SET);
    HAL_Delay(120);
}

/* Execute a command table: [count] then [cmd][nargs][data...] entries,
 * nargs with bit 0x80 set means "delay" and the next byte is ms. */
static void ST7735_ExecuteCmdList(const uint8_t *list)
{
    uint8_t numCmds = *list++;

    while (numCmds--)
    {
        uint8_t cmd = *list++;
        ST7735_WriteCmd(cmd);

        uint8_t numArgs = *list++;
        uint16_t delayMs = (numArgs & 0x80U) ? *list++ : 0U;
        numArgs &= 0x7FU;

        if (numArgs)
        {
            ST7735_WriteDataBuf(list, numArgs);
            list += numArgs;
        }

        if (delayMs)
        {
            if (delayMs == 255) delayMs = 500;
            HAL_Delay(delayMs);
        }
    }
}

/*===========================================================================
 * Public API
 *===========================================================================*/

void ST7735_Init(void)
{
    static const uint8_t initCmds1[] =
    {
        14,
        0x01, 0x80, 150,               /* SWRESET + 150ms delay */
        0x11, 0x80, 255,               /* SLPOUT + 500ms delay */
        0xB1, 3, 0x01, 0x2C, 0x2D,     /* FRMCTR1 */
        0xB2, 3, 0x01, 0x2C, 0x2D,     /* FRMCTR2 */
        0xB3, 6, 0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D,  /* FRMCTR3 */
        0xB4, 1, 0x07,                 /* INVCTR */
        0xC0, 3, 0xA2, 0x02, 0x84,     /* PWCTR1 */
        0xC1, 1, 0xC5,                 /* PWCTR2 */
        0xC2, 2, 0x0A, 0x00,           /* PWCTR3 */
        0xC3, 2, 0x8A, 0x2A,           /* PWCTR4 */
        0xC4, 2, 0x8A, 0xEE,           /* PWCTR5 */
        0xC5, 1, 0x0E,                 /* VMCTR1 */
        0x20, 0,                       /* INVOFF */
        0x3A, 1, 0x05                  /* COLMOD: 16-bit color */
    };

    static const uint8_t initCmds2[] =
    {
        2,
        0x2A, 4, 0x00, 0x00, 0x00, 0x4F,  /* CASET */
        0x2B, 4, 0x00, 0x00, 0x00, 0x9F   /* RASET */
    };

    static const uint8_t initCmds3[] =
    {
        4,
        0xE0, 16,
            0x02, 0x1C, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2D,
            0x29, 0x25, 0x2B, 0x39, 0x00, 0x01, 0x03, 0x10,  /* GMCTRP1 */
        0xE1, 16,
            0x03, 0x1D, 0x07, 0x06, 0x2E, 0x2C, 0x29, 0x2D,
            0x2E, 0x2E, 0x37, 0x3F, 0x00, 0x00, 0x02, 0x10,  /* GMCTRN1 */
        0x13, 0x80, 10,               /* NORON + 10ms */
        0x29, 0x80, 100               /* DISPON + 100ms */
    };

    ST7735_SetBacklight(1);

    ST7735_Select();
    ST7735_Reset();

    ST7735_ExecuteCmdList(initCmds1);

    /* MADCTL: see st7735_config.h (ST7735_MADCTL) */
    ST7735_WriteCmd(0x36);
    ST7735_WriteData(ST7735_MADCTL);

    ST7735_ExecuteCmdList(initCmds2);
    ST7735_ExecuteCmdList(initCmds3);

#if ST7735_INVERT
    ST7735_WriteCmd(0x21);   /* INVON */
#else
    ST7735_WriteCmd(0x20);   /* INVOFF */
#endif

    ST7735_Deselect();
}

void ST7735_SetBacklight(uint8_t on)
{
    HAL_GPIO_WritePin(LCD_EN_Port, LCD_EN_Pin,
                      on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void ST7735_SetAddrWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    /* Panel offsets come from st7735_config.h (ST7735_X_OFFSET/Y_OFFSET) */
    ST7735_WriteCmd(0x2A);
    ST7735_WriteData(0x00);
    ST7735_WriteData((x0 + ST7735_X_OFFSET) & 0xFF);
    ST7735_WriteData(0x00);
    ST7735_WriteData((x1 + ST7735_X_OFFSET) & 0xFF);

    ST7735_WriteCmd(0x2B);
    ST7735_WriteData(0x00);
    ST7735_WriteData((y0 + ST7735_Y_OFFSET) & 0xFF);
    ST7735_WriteData(0x00);
    ST7735_WriteData((y1 + ST7735_Y_OFFSET) & 0xFF);

    ST7735_WriteCmd(0x2C);   /* RAMWR */
}

/*===========================================================================
 * 阻塞式基础绘制
 *===========================================================================*/

void ST7735_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    uint8_t hi = (uint8_t)(color >> 8);
    uint8_t lo = (uint8_t)(color & 0xFF);
    uint16_t rowBytes = w * 2;
    uint16_t r, i;

    if (x >= ST7735_WIDTH || y >= ST7735_HEIGHT) return;
    if (x + w > ST7735_WIDTH)  w = ST7735_WIDTH - x;
    if (y + h > ST7735_HEIGHT) h = ST7735_HEIGHT - y;
    if (w == 0 || h == 0) return;

    for (i = 0; i < w; i++)
    {
        s_fbuf[0][i * 2]     = hi;
        s_fbuf[0][i * 2 + 1] = lo;
    }

    ST7735_Select();
    ST7735_SetAddrWindow(x, y, x + w - 1, y + h - 1);
    for (r = 0; r < h; r++)
    {
        ST7735_WriteDataBuf(s_fbuf[0], rowBytes);
    }
    ST7735_Deselect();
}

void ST7735_FillScreen(uint16_t color)
{
    ST7735_FillRect(0, 0, ST7735_WIDTH, ST7735_HEIGHT, color);
}

void ST7735_DrawPixel(uint16_t x, uint16_t y, uint16_t color)
{
    uint8_t data[2];
    if (x >= ST7735_WIDTH || y >= ST7735_HEIGHT) return;

    data[0] = (uint8_t)(color >> 8);
    data[1] = (uint8_t)(color & 0xFF);

    ST7735_Select();
    ST7735_SetAddrWindow(x, y, x, y);
    ST7735_WriteDataBuf(data, 2);
    ST7735_Deselect();
}

void ST7735_DrawHLine(uint16_t x, uint16_t y, uint16_t w, uint16_t color)
{
    if (y >= ST7735_HEIGHT) return;
    if (x >= ST7735_WIDTH) return;
    if (x + w > ST7735_WIDTH) w = ST7735_WIDTH - x;
    if (w == 0) return;
    ST7735_FillRect(x, y, w, 1, color);
}

void ST7735_DrawVLine(uint16_t x, uint16_t y, uint16_t h, uint16_t color)
{
    if (x >= ST7735_WIDTH) return;
    if (y >= ST7735_HEIGHT) return;
    if (y + h > ST7735_HEIGHT) h = ST7735_HEIGHT - y;
    if (h == 0) return;
    ST7735_FillRect(x, y, 1, h, color);
}

void ST7735_DrawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if (w < 2 || h < 2) return;
    ST7735_DrawHLine(x, y, w, color);
    ST7735_DrawHLine(x, (uint16_t)(y + h - 1), w, color);
    ST7735_DrawVLine(x, y, h, color);
    ST7735_DrawVLine((uint16_t)(x + w - 1), y, h, color);
}

/* Bresenham 直线 */
void ST7735_DrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color)
{
    int16_t dx = (x1 > x0) ? (int16_t)(x1 - x0) : (int16_t)(x0 - x1);
    int16_t dy = (y1 > y0) ? (int16_t)(y1 - y0) : (int16_t)(y0 - y1);
    int16_t sx = (x0 < x1) ? 1 : -1;
    int16_t sy = (y0 < y1) ? 1 : -1;
    int16_t err = (int16_t)(dx - dy);

    for (;;)
    {
        if (x0 >= 0 && y0 >= 0)
            ST7735_DrawPixel((uint16_t)x0, (uint16_t)y0, color);
        if (x0 == x1 && y0 == y1) break;
        {
            int16_t e2 = (int16_t)(2 * err);
            if (e2 > -dy) { err = (int16_t)(err - dy); x0 = (int16_t)(x0 + sx); }
            if (e2 <  dx) { err = (int16_t)(err + dx); y0 = (int16_t)(y0 + sy); }
        }
    }
}

/* 中点画圆（空心） */
void ST7735_DrawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color)
{
    int16_t x = r, y = 0, err = 0;

    while (x >= y)
    {
        ST7735_DrawPixel((uint16_t)(x0 + x), (uint16_t)(y0 + y), color);
        ST7735_DrawPixel((uint16_t)(x0 + y), (uint16_t)(y0 + x), color);
        ST7735_DrawPixel((uint16_t)(x0 - y), (uint16_t)(y0 + x), color);
        ST7735_DrawPixel((uint16_t)(x0 - x), (uint16_t)(y0 + y), color);
        ST7735_DrawPixel((uint16_t)(x0 - x), (uint16_t)(y0 - y), color);
        ST7735_DrawPixel((uint16_t)(x0 - y), (uint16_t)(y0 - x), color);
        ST7735_DrawPixel((uint16_t)(x0 + y), (uint16_t)(y0 - x), color);
        ST7735_DrawPixel((uint16_t)(x0 + x), (uint16_t)(y0 - y), color);
        if (err <= 0) { y = (int16_t)(y + 1); err = (int16_t)(err + 2 * y + 1); }
        if (err > 0)  { x = (int16_t)(x - 1); err = (int16_t)(err - 2 * x + 1); }
    }
}

/* 实心圆：逐条水平扫描线 */
void ST7735_FillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color)
{
    int16_t y;

    for (y = -r; y <= r; y++)
    {
        int16_t dx = ST7735_ISqrt((uint32_t)(r * r - y * y));
        if (dx < 0) dx = 0;
        ST7735_DrawHLine((uint16_t)(x0 - dx), (uint16_t)(y0 + y),
                         (uint16_t)(2 * dx + 1), color);
    }
}

/* 圆角矩形（空心） */
void ST7735_DrawRoundRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                          uint16_t r, uint16_t color)
{
    int16_t dy;

    if (w < 2 * r + 2 || h < 2 * r + 2) return;

    ST7735_DrawHLine((uint16_t)(x + r), y, (uint16_t)(w - 2 * r), color);
    ST7735_DrawHLine((uint16_t)(x + r), (uint16_t)(y + h - 1), (uint16_t)(w - 2 * r), color);
    ST7735_DrawVLine(x, (uint16_t)(y + r), (uint16_t)(h - 2 * r), color);
    ST7735_DrawVLine((uint16_t)(x + w - 1), (uint16_t)(y + r), (uint16_t)(h - 2 * r), color);

    for (dy = 0; dy <= (int16_t)r; dy++)
    {
        int16_t rx = ST7735_ISqrt((uint32_t)((int32_t)r * r - (int32_t)dy * dy));
        ST7735_DrawHLine((uint16_t)(x + r - rx), (uint16_t)(y + r - dy),
                         (uint16_t)(2 * rx + 1), color);
        ST7735_DrawHLine((uint16_t)(x + r - rx), (uint16_t)(y + h - 1 - r + dy),
                         (uint16_t)(2 * rx + 1), color);
    }
}

/* 圆角矩形（实心） */
void ST7735_FillRoundRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                          uint16_t r, uint16_t color)
{
    int16_t dy;

    if (w < 2 * r + 2 || h < 2 * r + 2) return;

    ST7735_FillRect(x, (uint16_t)(y + r), w, (uint16_t)(h - 2 * r), color);
    for (dy = 0; dy <= (int16_t)r; dy++)
    {
        int16_t rx = ST7735_ISqrt((uint32_t)((int32_t)r * r - (int32_t)dy * dy));
        ST7735_DrawHLine((uint16_t)(x + r - rx), (uint16_t)(y + r - dy),
                         (uint16_t)(2 * rx + 1), color);
        ST7735_DrawHLine((uint16_t)(x + r - rx), (uint16_t)(y + h - 1 - r + dy),
                         (uint16_t)(2 * rx + 1), color);
    }
}

/*===========================================================================
 * 文字
 *===========================================================================*/

void ST7735_DrawChar(uint16_t x, uint16_t y, char ch, uint16_t color, uint16_t bg)
{
    const uint8_t *glyph;
    uint8_t hiC = (uint8_t)(color >> 8);
    uint8_t loC = (uint8_t)(color & 0xFF);
    uint8_t hiB = (uint8_t)(bg >> 8);
    uint8_t loB = (uint8_t)(bg & 0xFF);
    uint8_t row, col;

    if (x + FONT5X7_WIDTH > ST7735_WIDTH || y + FONT5X7_HEIGHT > ST7735_HEIGHT)
        return;
    if (ch < 0x20 || ch > 0x7E) ch = 0x20;

    glyph = Font5x7[ch - 0x20];

    ST7735_Select();
    ST7735_SetAddrWindow(x, y, x + FONT5X7_WIDTH - 1, y + FONT5X7_HEIGHT - 1);

    for (row = 0; row < FONT5X7_HEIGHT; row++)
    {
        for (col = 0; col < FONT5X7_WIDTH; col++)
        {
            uint8_t idx = col * 2;
            if (glyph[col] & (1U << row))
            {
                s_fbuf[0][idx]     = hiC;
                s_fbuf[0][idx + 1] = loC;
            }
            else
            {
                s_fbuf[0][idx]     = hiB;
                s_fbuf[0][idx + 1] = loB;
            }
        }
        ST7735_WriteDataBuf(s_fbuf[0], FONT5X7_WIDTH * 2);
    }

    ST7735_Deselect();
}

void ST7735_DrawString(uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t bg)
{
    uint16_t cx = x;
    uint16_t cy = y;

    while (*str)
    {
        if (*str == 0x0A)   /* LF: newline */
        {
            cx = x;
            cy += FONT5X7_HEIGHT + 1;
            str++;
            continue;
        }

        if (cx + FONT5X7_WIDTH > ST7735_WIDTH)  /* wrap */
        {
            cx = x;
            cy += FONT5X7_HEIGHT + 1;
        }

        ST7735_DrawChar(cx, cy, *str, color, bg);
        cx += FONT5X7_WIDTH + 1;
        str++;
    }
}

uint16_t ST7735_StringWidth(const char *str)
{
    uint16_t len = 0;
    while (*str)
    {
        if (*str == 0x0A) { str++; continue; }
        len += FONT5X7_WIDTH + 1;
        str++;
    }
    return (len > 0) ? (uint16_t)(len - 1) : 0;
}

/*===========================================================================
 * DMA 加速接口
 *===========================================================================*/

void ST7735_FillRect_DMA(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    uint8_t hi = (uint8_t)(color >> 8);
    uint8_t lo = (uint8_t)(color & 0xFF);
    uint16_t rowBytes;
    uint16_t r, i;

    if (x >= ST7735_WIDTH || y >= ST7735_HEIGHT) return;
    if (x + w > ST7735_WIDTH)  w = ST7735_WIDTH - x;
    if (y + h > ST7735_HEIGHT) h = ST7735_HEIGHT - y;
    if (w == 0 || h == 0) return;

    rowBytes = (uint16_t)(w * 2);

    for (i = 0; i < w; i++)
    {
        s_fbuf[0][i * 2]     = hi;
        s_fbuf[0][i * 2 + 1] = lo;
    }

    ST7735_Select();
    ST7735_SetAddrWindow(x, y, (uint16_t)(x + w - 1), (uint16_t)(y + h - 1));
    for (r = 0; r < h; r++)
    {
        ST7735_DMA_Start(s_fbuf[0], rowBytes);
        ST7735_DMA_Wait();
    }
    ST7735_Deselect();
}

void ST7735_FillScreen_DMA(uint16_t color)
{
    ST7735_FillRect_DMA(0, 0, ST7735_WIDTH, ST7735_HEIGHT, color);
}

void ST7735_Blit565_DMA(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *pix)
{
    uint16_t row, i;

    if (x >= ST7735_WIDTH || y >= ST7735_HEIGHT) return;
    if (x + w > ST7735_WIDTH)  w = ST7735_WIDTH - x;
    if (y + h > ST7735_HEIGHT) h = ST7735_HEIGHT - y;
    if (w == 0 || h == 0) return;

    ST7735_Select();
    ST7735_SetAddrWindow(x, y, (uint16_t)(x + w - 1), (uint16_t)(y + h - 1));
    for (row = 0; row < h; row++)
    {
        const uint16_t *src = pix + (uint32_t)row * w;
        for (i = 0; i < w; i++)
        {
            s_fbuf[0][i * 2]     = (uint8_t)(src[i] >> 8);
            s_fbuf[0][i * 2 + 1] = (uint8_t)(src[i] & 0xFF);
        }
        ST7735_DMA_Start(s_fbuf[0], (uint16_t)(w * 2));
        ST7735_DMA_Wait();
    }
    ST7735_Deselect();
}

/* 双缓冲 DMA 全屏渲染：乒乓缓冲让计算与传输重叠 */
void ST7735_DrawFrame(ST7735_RowRenderFn fn)
{
    uint8_t *cur = s_fbuf[0];
    uint8_t *nxt = s_fbuf[1];
    uint8_t *tmp;
    uint16_t y;

    if (fn == NULL) return;

    ST7735_Select();
    ST7735_SetAddrWindow(0, 0, ST7735_WIDTH - 1, ST7735_HEIGHT - 1);

    /* 第 0 行先渲染并启动 DMA */
    fn(0, cur, ST7735_WIDTH);
    ST7735_DMA_Start(cur, (uint16_t)(ST7735_WIDTH * 2));

    for (y = 1; y < ST7735_HEIGHT; y++)
    {
        /* CPU 渲染下一行（与上一行的 DMA 传输并行） */
        fn(y, nxt, ST7735_WIDTH);
        ST7735_DMA_Wait();
        tmp = cur; cur = nxt; nxt = tmp;   /* 交换缓冲 */
        ST7735_DMA_Start(cur, (uint16_t)(ST7735_WIDTH * 2));
    }
    ST7735_DMA_Wait();
    ST7735_Deselect();
}

/*===========================================================================
 * HAL 回调覆写：DMA 完成 / 出错时清除忙标志
 *===========================================================================*/
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance == SPI1)
    {
        s_dmaDone = 1;
    }
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance == SPI1)
    {
        s_dmaDone = 1;
    }
}
