#include "board.h"
#include "system.h"
#include "debug.h"
#include "progmode.h"
#include "lcd.h"
#include "keypad.h"
#include "bk4819.h"
#include "spiflash.h"
#include "adc.h"
#include "radio.h"
#include "shell.h"
#include "ui.h"
#include "voice.h"
#include "tx.h"
#include "splash.h"

/* talkpod-firmware checks these markers before flashing */
const char __attribute__((used, section(".rodata.markers"))) fw_marker1[] = "PROGRAMJC37U";
const char __attribute__((used, section(".rodata.markers"))) fw_marker2[] = "TalkpodA36Plus";
const char __attribute__((used, section(".rodata.markers"))) fw_version[]  = " V0.98";

int main(void)
{
    debug_init(57600);
    debug_puts("\r\n=== A36SE FW v0.98 ===\r\nbootloader tag ");
    debug_write((const uint8_t *)0x08000300U, 7);
    debug_puts("\r\n");

    systick_init();
    __enable_irq();                 /* the startup code left interrupts masked */

    board_init();
    lcd_init();
    keypad_init();
    bk4819_init();
    board_rf_path(0, RF_OFF);       /* as stock before its first RX: REG_33 = 0 */
    spiflash_init();
    splash_play();
    adc_init();
    radio_init();
    tx_init();
    ui_init();

    uint32_t next = millis();
    for (;;) {
        progmode_poll();
        fwdgt_feed();
        voice_poll();
        if ((int32_t)(millis() - next) >= 0) {
            next += KEY_SCAN_MS;
            keypad_scan();
            board_power_poll();
            key_event_t ev;
            while (keypad_get(&ev))
                ui_key(&ev);
            ui_tick();
        }
        shell_poll();
    }
}
