/* Boot splash: an animation from tools/splash.py at FLASH_SPLASH, played once; any key skips it.
 * Each frame is a changed rectangle with its own palette and PackBits-coded indices (format in
 * tools/splash.py), streamed from the SPI flash to the LCD.
 */
#include "splash.h"
#include "keypad.h"
#include "lcd.h"
#include "spiflash.h"
#include "system.h"

static uint8_t fbuf[256];
static uint32_t faddr;
static uint16_t fpos = sizeof fbuf;

static uint8_t next_byte(void)
{
    if (fpos == sizeof fbuf) {
        spiflash_read(faddr, fbuf, sizeof fbuf);
        faddr += sizeof fbuf;
        fpos = 0;
    }
    return fbuf[fpos++];
}

static void read_bytes(void *dst, uint32_t n)
{
    uint8_t *d = dst;
    while (n--)
        *d++ = next_byte();
}

/* a key pressed: 1, once it is let go again (its release must not reach the UI) */
static int skipped(void)
{
    key_event_t ev;
    int pressed = 0;
    keypad_scan();
    while (keypad_get(&ev))
        pressed |= ev.type == KEV_PRESS;
    if (!pressed)
        return 0;
    for (uint32_t t = millis(); millis() - t < 3000;) {
        uint32_t next = millis() + KEY_SCAN_MS;
        while ((int32_t)(millis() - next) < 0) {}
        fwdgt_feed();
        keypad_scan();
        while (keypad_get(&ev))
            if (ev.type == KEV_RELEASE)
                return 1;
    }
    return 1;
}

/* waits until `until`, scanning keys; 1 if a key was pressed */
static int wait_until(uint32_t until)
{
    while ((int32_t)(millis() - until) < 0) {
        fwdgt_feed();
        uint32_t t = millis() + KEY_SCAN_MS;
        while ((int32_t)(millis() - t) < 0 && (int32_t)(millis() - until) < 0) {}
        if (skipped())
            return 1;
    }
    return 0;
}

int splash_play(void)
{
    uint8_t head[12];
    static uint16_t pal[256];
    spiflash_read(FLASH_SPLASH, head, sizeof head);
    if (head[0] != 'A' || head[1] != '3' || head[2] != '6' || head[3] != 'G')
        return 0;
    uint16_t frames = (uint16_t)(head[4] | head[5] << 8), bg = (uint16_t)(head[6] | head[7] << 8);
    uint32_t total = head[8] | head[9] << 8 | (uint32_t)head[10] << 16 | (uint32_t)head[11] << 24;
    if (!frames || total > FLASH_SETTINGS - FLASH_SPLASH)
        return 0;
    faddr = FLASH_SPLASH + sizeof head;
    fpos = sizeof fbuf;
    lcd_fill(0, 0, LCD_W, LCD_H, bg);
    lcd_backlight(1);
    uint32_t due = millis();
    for (uint16_t f = 0; f < frames; f++) {
        uint8_t fh[8];
        uint32_t len;
        read_bytes(fh, sizeof fh);
        uint16_t delay = (uint16_t)(fh[0] | fh[1] << 8), colours = (uint16_t)(fh[6] | fh[7] << 8);
        int x = fh[2], y = fh[3], w = fh[4], h = fh[5];
        if (colours > 256 || x + w > LCD_W || y + h > LCD_H)
            break;                                      /* not a splash this firmware wrote */
        read_bytes(pal, colours * 2U);
        read_bytes(&len, 4);
        if (w && h) {
            int left = w * h;
            lcd_begin(x, y, w, h);
            while (left > 0 && len) {
                uint8_t n = next_byte();
                len--;
                if (n < 128) {
                    for (int i = 0; i <= n && len; i++, len--, left--)
                        lcd_push(pal[next_byte()]);
                } else {
                    uint16_t c = pal[next_byte()];
                    len--;
                    for (int i = 0; i < n - 125; i++, left--)
                        lcd_push(c);
                }
                fwdgt_feed();
            }
            while (len--)                               /* keep the stream aligned */
                next_byte();
            lcd_end();
        }
        due += delay;
        if (wait_until(due))
            break;
    }
    return 1;
}
