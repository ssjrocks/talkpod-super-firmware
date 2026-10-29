#ifndef GFX_H
#define GFX_H

#include <stdint.h>

/* Fixed-cell 1-bit font: glyphs first..last, each h rows of `stride` bytes, MSB = leftmost pixel. */
typedef struct {
    uint8_t w, h, stride, first, last;
    const uint8_t *bits;
} font_t;

extern const font_t font_6x10;     /* ASCII text */
extern const font_t font_10x20;    /* space to ':' only: frequencies and numbers */

/* Draws text with an opaque background, clipped at the right screen edge; returns the x after it. */
int gfx_text(int x, int y, const font_t *f, const char *s, uint16_t fg, uint16_t bg);
int gfx_text_width(const font_t *f, const char *s);

#endif
