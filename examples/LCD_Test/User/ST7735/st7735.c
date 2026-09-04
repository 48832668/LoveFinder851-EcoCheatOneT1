/**
 * @file st7735.c
 * @brief ST7735 160x80 (0.96 inch) LCD driver - C implementation, blocking SPI
 *
 * Init sequence taken from LoveFinder491_PowerOneT2 LoveFinderLib/ST7735
 * (panel A: BGR, offsets 0/24, no inversion, DEG_0 landscape 160x80).
 */

#include "st7735.h"
#include "fonts.h"
#include "spi.h"   /* hspi1 */
#include "gpio.h"  /* LCD_DC / LCD_RESET / LCD_CS / LCD_EN */

/* Inversion is configured in st7735_config.h (ST7735_INVERT) */

/* Static line buffer used for pixel data (2 bytes per pixel) */
static uint8_t s_lineBuf[ST7735_WIDTH * 2];

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

    /* Enable LCD backlight */
    HAL_GPIO_WritePin(LCD_EN_Port, LCD_EN_Pin, GPIO_PIN_SET);

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
        s_lineBuf[i * 2]     = hi;
        s_lineBuf[i * 2 + 1] = lo;
    }

    ST7735_Select();
    ST7735_SetAddrWindow(x, y, x + w - 1, y + h - 1);
    for (r = 0; r < h; r++)
    {
        ST7735_WriteDataBuf(s_lineBuf, rowBytes);
    }
    ST7735_Deselect();
}

void ST7735_FillScreen(uint16_t color)
{
    ST7735_FillRect(0, 0, ST7735_WIDTH, ST7735_HEIGHT, color);
}

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
                s_lineBuf[idx]     = hiC;
                s_lineBuf[idx + 1] = loC;
            }
            else
            {
                s_lineBuf[idx]     = hiB;
                s_lineBuf[idx + 1] = loB;
            }
        }
        ST7735_WriteDataBuf(s_lineBuf, FONT5X7_WIDTH * 2);
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
