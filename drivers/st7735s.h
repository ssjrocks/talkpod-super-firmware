#ifndef ST7735S_H
#define ST7735S_H

#include <stdint.h>

void lcd_init(void);
void lcd_set_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
void lcd_fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void lcd_fill_screen(uint16_t color);
void lcd_backlight(int on);

#endif
