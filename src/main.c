#include "board.h"
#include "system.h"
#include "debug.h"
#include "progmode.h"
#include "lcd.h"

/* talkpod-firmware checks these markers before flashing */
const char __attribute__((used, section(".rodata.markers"))) fw_marker1[] = "PROGRAMJC37U";
const char __attribute__((used, section(".rodata.markers"))) fw_marker2[] = "TalkpodA36Plus";
const char __attribute__((used, section(".rodata.markers"))) fw_version[]  = " V0.4";

/* Test pattern: 1-pixel white border (a missing or doubled edge shows a panel offset), a red
   square in the top-left corner and a blue one bottom-right (orientation), eight colour bars
   (colour order), and a black-to-white ramp along the bottom. */
static void test_pattern(void)
{
    static const uint16_t bars[8] = {
        RGB(255, 255, 255), RGB(255, 255, 0), RGB(0, 255, 255), RGB(0, 255, 0),
        RGB(255, 0, 255), RGB(255, 0, 0), RGB(0, 0, 255), RGB(128, 128, 128),
    };
    lcd_fill(0, 0, LCD_W, LCD_H, 0x0000);
    for (int i = 0; i < 8; i++)
        lcd_fill(8 + i * 14, 20, 14, 70, bars[i]);
    lcd_begin(8, 96, 112, 16);
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 112; x++) {
            int v = x * 255 / 111;
            lcd_push(RGB(v, v, v));
        }
    lcd_end();
    lcd_fill(2, 2, 12, 12, RGB(255, 0, 0));
    lcd_fill(LCD_W - 14, LCD_H - 14, 12, 12, RGB(0, 0, 255));
    lcd_fill(0, 0, LCD_W, 1, 0xFFFF);
    lcd_fill(0, LCD_H - 1, LCD_W, 1, 0xFFFF);
    lcd_fill(0, 0, 1, LCD_H, 0xFFFF);
    lcd_fill(LCD_W - 1, 0, 1, LCD_H, 0xFFFF);
}

int main(void)
{
    debug_init(57600);
    debug_puts("\r\n=== A36SE FW v0.4 ===\r\n");

    systick_init();
    __enable_irq();                 /* the startup code left interrupts masked */

    uint32_t t0 = millis();
    lcd_init();
    test_pattern();
    debug_puts("LCD init + test pattern took ");
    debug_hex32(millis() - t0);
    debug_puts(" ms (hex)\r\n");

    uint32_t beat = 0;
    for (;;) {
        delay_ms(5000);
        debug_puts("beat ");
        debug_hex32(++beat);
        debug_puts("\r\n");
    }
}
