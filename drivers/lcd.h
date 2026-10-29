#ifndef LCD_H
#define LCD_H

#include <stdint.h>

#define LCD_W 128
#define LCD_H 128

/* RGB565 */
#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

void lcd_init(void);                 /* needs delay_ms() (SysTick + interrupts on) */
void lcd_backlight(int on);
void lcd_fill(int x, int y, int w, int h, uint16_t color);
void lcd_begin(int x, int y, int w, int h);   /* then lcd_push() w*h pixels, then lcd_end() */
void lcd_push(uint16_t color);
void lcd_end(void);

#endif
