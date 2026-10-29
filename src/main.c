#include "board.h"
#include "system.h"
#include "debug.h"
#include "progmode.h"
#include "lcd.h"
#include "gfx.h"
#include "keypad.h"

/* talkpod-firmware checks these markers before flashing */
const char __attribute__((used, section(".rodata.markers"))) fw_marker1[] = "PROGRAMJC37U";
const char __attribute__((used, section(".rodata.markers"))) fw_marker2[] = "TalkpodA36Plus";
const char __attribute__((used, section(".rodata.markers"))) fw_version[]  = " V0.5";

#define DBG_IDCODE  (*(volatile uint32_t *)0xE0042000U)   /* MCU device/revision ID */
#define FLASH_KB    (*(volatile uint16_t *)0x1FFFF7E0U)

#define C_BG     RGB(0, 0, 0)
#define C_BAR    RGB(0, 40, 110)
#define C_TEXT   RGB(255, 255, 255)
#define C_DIM    RGB(140, 140, 140)
#define C_FREQ   RGB(255, 210, 0)
#define C_PTT_ON RGB(220, 0, 0)
#define C_PTT_OFF RGB(50, 50, 50)

static const char *const event_names[] = {"press", "long", "repeat", "release"};

static void hex32(char *out, uint32_t v)
{
    for (int i = 0; i < 8; i++)
        out[i] = "0123456789ABCDEF"[(v >> (28 - 4 * i)) & 0xF];
    out[8] = 0;
}

static void dec(char *out, uint32_t v)
{
    char tmp[11];
    int n = 0;
    do {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n)
        *out++ = tmp[--n];
    *out = 0;
}

/* Draws s then blanks the rest of a field `width` pixels wide, so shorter text replaces longer. */
static void field(int x, int y, int width, const char *s, uint16_t fg)
{
    int end = gfx_text(x, y, &font_6x10, s, fg, C_BG);
    if (end < x + width)
        lcd_fill(end, y, x + width - end, font_6x10.h, C_BG);
}

static void draw_ptt(int on)
{
    uint16_t c = on ? C_PTT_ON : C_PTT_OFF;
    lcd_fill(44, 104, 40, 18, c);
    gfx_text(55, 108, &font_6x10, "PTT", C_TEXT, c);
}

static void draw_static(uint32_t idcode)
{
    char buf[12];
    lcd_fill(0, 0, LCD_W, 13, C_BAR);
    gfx_text(4, 2, &font_6x10, "A36SE CFW v0.5", C_TEXT, C_BAR);
    gfx_text(4, 17, &font_6x10, "MCU", C_DIM, C_BG);
    hex32(buf, idcode);
    gfx_text(28, 17, &font_6x10, buf, C_DIM, C_BG);
    gfx_text(19, 32, &font_10x20, "446.00625", C_FREQ, C_BG);
    gfx_text(4, 60, &font_6x10, "Key", C_DIM, C_BG);
    gfx_text(4, 72, &font_6x10, "Event", C_DIM, C_BG);
    gfx_text(4, 84, &font_6x10, "Count", C_DIM, C_BG);
    field(46, 60, 78, "-", C_TEXT);
    field(46, 72, 78, "-", C_TEXT);
    field(46, 84, 78, "0", C_TEXT);
    draw_ptt(0);
}

int main(void)
{
    debug_init(57600);
    debug_puts("\r\n=== A36SE FW v0.5 ===\r\n");
    uint32_t idcode = DBG_IDCODE;
    debug_puts("idcode ");
    debug_hex32(idcode);
    debug_puts(" flash ");
    debug_hex16(FLASH_KB);
    debug_puts(" KB(hex)\r\n");

    systick_init();
    __enable_irq();                 /* the startup code left interrupts masked */

    lcd_init();
    keypad_init();
    draw_static(idcode);

    uint32_t next_scan = millis(), next_beat = millis() + 5000, count = 0;
    char buf[12];
    for (;;) {
        progmode_poll();
        fwdgt_feed();
        uint32_t now = millis();
        if ((int32_t)(now - next_scan) >= 0) {
            next_scan += KEY_SCAN_MS;
            keypad_scan();
        }

        key_event_t ev;
        while (keypad_get(&ev)) {
            debug_puts("key ");
            debug_puts(key_name(ev.key));
            debug_puts(" ");
            debug_puts(event_names[ev.type]);
            debug_puts("\r\n");
            field(46, 60, 78, key_name(ev.key), C_TEXT);
            field(46, 72, 78, event_names[ev.type], C_TEXT);
            if (ev.type == KEV_PRESS) {
                dec(buf, ++count);
                field(46, 84, 78, buf, C_TEXT);
            }
            if (ev.key == KEY_PTT)
                draw_ptt(ev.type != KEV_RELEASE);
        }

        if ((int32_t)(now - next_beat) >= 0) {
            next_beat += 5000;
            debug_puts("beat\r\n");
        }
    }
}
