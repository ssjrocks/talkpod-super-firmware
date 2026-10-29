#include "gd32f30x.h"
#include "debug.h"

/* Bootloader leaves USART0 fully configured at 115200 baud (48 MHz):
   PA9 in AF1 mode, BRR=417, UEN+TEN+REN enabled.
   DO NOT reconfigure PA9 or USART0 — just write to the data register. */

#define USART0_STAT_REG  (*(volatile uint32_t *)0x40013800U)
#define USART0_DATA_REG  (*(volatile uint32_t *)0x40013804U)
#define TXE_BIT          (1U << 7)

void debug_init(void)
{
    /* nothing to do — bootloader config is already correct */
}

void debug_putc(char c)
{
    uint32_t timeout = 200000;
    while (!(USART0_STAT_REG & TXE_BIT)) {
        if (--timeout == 0) return;
    }
    USART0_DATA_REG = (uint8_t)c;
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
