/* The stock firmware's serial programming handshake, as far as flashing needs it.
 *
 *   PC -> "PROGRAMJC37U"      radio -> 0x06
 *   PC -> 'F'                 radio -> 8-byte identification
 *   PC -> 'E'                 (leave programming mode)
 *   PC -> "UPDATE"            radio -> 0x06, then reset: the bootloader listens for an INTO_BOOT
 *                             frame at every start, so talkpod-firmware can flash with no buttons.
 *
 * The stock UPDATE handler does exactly this (ACK, a message on screen, NVIC system reset).
 */
#include <stdint.h>
#include <string.h>
#include "debug.h"
#include "progmode.h"

static const char MAGIC[] = "PROGRAMJC37U";
/* Not a stock fingerprint, so CHIRP / Talkpod Manager won't mistake us for stock firmware. */
static const uint8_t IDENT[8] = {'A', '3', '6', 'C', 'F', 'W', '0', '1'};

static char window[12];
static uint8_t in_prog;
static uint8_t upd_len;

static void system_reset(void)
{
    debug_flush();
    __asm volatile ("dsb");
    *(volatile uint32_t *)0xE000ED0CU = 0x05FA0004U | (*(volatile uint32_t *)0xE000ED0CU & 0x700U);
    __asm volatile ("dsb");
    for (;;) {}
}

void progmode_poll(void)
{
    int c;
    while ((c = debug_getc()) >= 0) {
        if (in_prog) {
            if (upd_len) {                          /* collecting "UPDATE" */
                static const char UPD[] = "UPDATE";
                if (c == UPD[upd_len]) {
                    if (++upd_len == 6) {
                        debug_putc(0x06);
                        system_reset();
                    }
                    continue;
                }
                upd_len = 0;
                in_prog = 0;
            } else if (c == 'F') {
                debug_write(IDENT, sizeof IDENT);
                continue;
            } else if (c == 'E') {
                in_prog = 0;
                continue;
            } else if (c == 'U') {
                upd_len = 1;
                continue;
            }
        }
        memmove(window, window + 1, sizeof window - 1);
        window[sizeof window - 1] = (char)c;
        if (memcmp(window, MAGIC, sizeof window) == 0) {
            debug_putc(0x06);
            in_prog = 1;
            upd_len = 0;
        }
    }
}
