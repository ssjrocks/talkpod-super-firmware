#ifndef GFX_H
#define GFX_H

#include <stdint.h>

/* Fixed-cell 1-bit font: glyphs first..last, each h rows of `stride` bytes, MSB = leftmost pixel. */
typedef struct {
    uint8_t w, h, stride, first, last;
    const uint8_t *bits;
} font_t;

/* Proportional anti-aliased font, 2 bits per pixel (tools/mkaafont.py): glyph rows padded to 4
   pixels, 0 = background .. 3 = full ink. */
typedef struct {
    uint8_t h, first, last;
    const uint8_t *widths;
    const uint16_t *offsets;
    const uint8_t *bits;
} aafont_t;

extern const font_t font_5x8;      /* small labels */
extern const font_t font_6x10;     /* ASCII text */
extern const font_t font_7x13b;    /* names, titles */
extern const font_t font_10x20;    /* space to ':' only: frequencies and numbers */
extern const aafont_t font_digits21;   /* '-' '.' '/' '0'-'9' */
extern const aafont_t font_digits12;

/* 1-bit icon, rows of up to 16 pixels, MSB = leftmost */
typedef struct {
    uint8_t w, h;
    const uint16_t *rows;
} icon_t;

extern const icon_t icon_battery, icon_envelope, icon_lock;

/* Draws text with an opaque background, clipped at the right screen edge; returns the x after it. */
int gfx_text(int x, int y, const font_t *f, const char *s, uint16_t fg, uint16_t bg);
int gfx_text_width(const font_t *f, const char *s);
int gfx_aa_text(int x, int y, const aafont_t *f, const char *s, uint16_t fg, uint16_t bg);
int gfx_aa_width(const aafont_t *f, const char *s);
void gfx_rrect(int x, int y, int w, int h, uint16_t c);      /* corners cut by one pixel */
void gfx_icon(int x, int y, const icon_t *ic, uint16_t fg, uint16_t bg);
uint16_t gfx_mix(uint16_t a, uint16_t b, int num, int den);  /* a + (b - a) * num / den */

#endif
