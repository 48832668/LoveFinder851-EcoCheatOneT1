/**
 * @file fonts.h
 * @brief 5x7 ASCII font for ST7735 LCD (chars 0x20..0x7E)
 *
 * Format: one 5-byte entry per character, each byte is one column,
 * bit 0..6 = row 0..6 (bit 0 is the top row).
 */

#ifndef FONTS_H
#define FONTS_H

#include <stdint.h>

#define FONT5X7_WIDTH   5
#define FONT5X7_HEIGHT  7
#define FONT5X7_CHARS   95   /* 0x20 .. 0x7E */

extern const uint8_t Font5x7[FONT5X7_CHARS][FONT5X7_WIDTH];

#endif /* FONTS_H */
