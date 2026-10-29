#include <stdint.h>
#include "debug.h"

/* USART0 on PA9 (TX) / PA10 (RX, shared with PTT). The bootloader leaves it enabled at 115200 baud
   for its 48 MHz clock. We switch to 57600 baud, the rate the stock firmware, CHIRP and
   talkpod-firmware use, and turn off its receive interrupt (we poll). */

#define USART0_STAT  (*(volatile uint32_t *)0x40013800U)
#define USART0_DATA  (*(volatile uint32_t *)0x40013804U)
#define USART0_BAUD  (*(volatile uint32_t *)0x40013808U)
#define USART0_CTL0  (*(volatile uint32_t *)0x4001380CU)

#define STAT_TC    (1U << 6)
#define STAT_TXE   (1U << 7)
#define STAT_RBNE  (1U << 5)
#define STAT_ORERR (1U << 3)
#define CTL0_UEN   (1U << 13)
#define CTL0_RBNEIE (1U << 5)

#define CLOCK_HZ 48000000U

void debug_init(uint32_t baud)
{
    uint32_t t = 1000000;
    while (!(USART0_STAT & STAT_TC) && --t) {}     /* let anything in flight finish */
    USART0_CTL0 &= ~CTL0_UEN;
    USART0_BAUD = (CLOCK_HZ + baud / 2) / baud;    /* 48 MHz / 57600 = 833 */
    USART0_CTL0 = (USART0_CTL0 & ~CTL0_RBNEIE) | CTL0_UEN;
}

void debug_putc(char c)
{
    uint32_t timeout = 200000;
    while (!(USART0_STAT & STAT_TXE)) {
        if (--timeout == 0) return;
    }
    USART0_DATA = (uint8_t)c;
}

void debug_flush(void)
{
    uint32_t timeout = 400000;
    while (!(USART0_STAT & STAT_TC) && --timeout) {}
}

int debug_getc(void)
{
    uint32_t st = USART0_STAT;
    if (st & STAT_ORERR) {          /* overrun: reading STAT then DATA clears it */
        (void)USART0_DATA;
        return -1;
    }
    if (st & STAT_RBNE)
        return (int)(USART0_DATA & 0xFF);
    return -1;
}

void debug_write(const uint8_t *p, uint32_t n)
{
    while (n--)
        debug_putc((char)*p++);
}

void debug_puts(const char *s)
{
    while (*s)
        debug_putc(*s++);
}

static const char hex_chars[] = "0123456789ABCDEF";

void debug_hex32(uint32_t v)
{
    for (int i = 28; i >= 0; i -= 4)
        debug_putc(hex_chars[(v >> i) & 0xF]);
}

void debug_hex16(uint16_t v)
{
    for (int i = 12; i >= 0; i -= 4)
        debug_putc(hex_chars[(v >> i) & 0xF]);
}
