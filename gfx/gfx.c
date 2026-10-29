/* Text drawing straight to the LCD: there is no frame buffer (128 x 128 x 2 bytes is 32 KB,
 * twice the RAM), so each glyph is one LCD window filled pixel by pixel. */
#include "gfx.h"
#include "lcd.h"

static void glyph(int x, int y, const font_t *f, char c, uint16_t fg, uint16_t bg)
{
    uint8_t ch = (uint8_t)c;
    int w = f->w;
    if (x + w > LCD_W)
        w = LCD_W - x;
    lcd_begin(x, y, w, f->h);
    if (ch < f->first || ch > f->last) {
        for (int n = w * f->h; n > 0; n--)
            lcd_push(bg);
    } else {
        const uint8_t *p = f->bits + (ch - f->first) * f->h * f->stride;
        for (int row = 0; row < f->h; row++, p += f->stride)
            for (int col = 0; col < w; col++)
                lcd_push((p[col >> 3] & (0x80 >> (col & 7))) ? fg : bg);
    }
    lcd_end();
}

int gfx_text(int x, int y, const font_t *f, const char *s, uint16_t fg, uint16_t bg)
{
    for (; *s && x < LCD_W; s++, x += f->w)
        glyph(x, y, f, *s, fg, bg);
    return x;
}

int gfx_text_width(const font_t *f, const char *s)
{
    int n = 0;
    while (s[n])
        n++;
    return n * f->w;
}
