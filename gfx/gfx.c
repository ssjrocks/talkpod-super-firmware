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

uint16_t gfx_mix(uint16_t a, uint16_t b, int num, int den)
{
    int ar = a >> 11, ag = (a >> 5) & 63, ab = a & 31;
    int br = b >> 11, bg = (b >> 5) & 63, bb = b & 31;
    return (uint16_t)((ar + (br - ar) * num / den) << 11 | (ag + (bg - ag) * num / den) << 5 |
                      (ab + (bb - ab) * num / den));
}

int gfx_aa_text(int x, int y, const aafont_t *f, const char *s, uint16_t fg, uint16_t bg)
{
    uint16_t shade[4] = {bg, gfx_mix(bg, fg, 1, 3), gfx_mix(bg, fg, 2, 3), fg};
    for (; *s; s++) {
        uint8_t ch = (uint8_t)*s;
        if (ch < f->first || ch > f->last)
            continue;
        int w = f->widths[ch - f->first], stride = (w + 3) / 4, vis = w;
        if (x + vis > LCD_W)
            vis = LCD_W - x;
        if (vis <= 0)
            break;
        const uint8_t *p = f->bits + f->offsets[ch - f->first];
        lcd_begin(x, y, vis, f->h);
        for (int row = 0; row < f->h; row++, p += stride)
            for (int col = 0; col < vis; col++)
                lcd_push(shade[(p[col >> 2] >> (6 - 2 * (col & 3))) & 3]);
        lcd_end();
        x += w;
    }
    return x;
}

int gfx_aa_width(const aafont_t *f, const char *s)
{
    int w = 0;
    for (; *s; s++)
        if ((uint8_t)*s >= f->first && (uint8_t)*s <= f->last)
            w += f->widths[(uint8_t)*s - f->first];
    return w;
}

void gfx_rrect(int x, int y, int w, int h, uint16_t c)
{
    lcd_fill(x + 1, y, w - 2, 1, c);
    lcd_fill(x, y + 1, w, h - 2, c);
    lcd_fill(x + 1, y + h - 1, w - 2, 1, c);
}

void gfx_icon(int x, int y, const icon_t *ic, uint16_t fg, uint16_t bg)
{
    lcd_begin(x, y, ic->w, ic->h);
    for (int row = 0; row < ic->h; row++)
        for (int col = 0; col < ic->w; col++)
            lcd_push((ic->rows[row] & (0x8000 >> col)) ? fg : bg);
    lcd_end();
}

static const uint16_t battery_rows[] = {
    0xFFF8, 0x8008, 0x800E, 0x800E, 0x800E, 0x800E, 0x8008, 0xFFF8,
};
static const uint16_t envelope_rows[] = {
    0xFFE0, 0xC060, 0xA0A0, 0x9120, 0x8A20, 0x8420, 0x8020, 0xFFE0,
};
static const uint16_t lock_rows[] = {
    0x3800, 0x4400, 0x4400, 0xFE00, 0xFE00, 0xEE00, 0xFE00, 0xFE00,
};

const icon_t icon_battery = {15, 8, battery_rows};
const icon_t icon_envelope = {11, 8, envelope_rows};
const icon_t icon_lock = {7, 8, lock_rows};
