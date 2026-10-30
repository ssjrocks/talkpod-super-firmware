#include <stdint.h>
#include "debug.h"
#include "gd32f3x0.h"

/* USART0 on PA9 (TX) / PA10 (RX, shared with PTT). The AT32F421 USART has the older register
   layout (STAT, DATA, BAUD, CTL0 at +0, +4, +8, +C), not GD32F3x0's, so it is driven directly. The
   bootloader leaves it enabled at 115200 baud for its 48 MHz clock. We switch to 57600 baud, the
   rate the stock firmware, CHIRP and talkpod-firmware use, and turn off its receive interrupt (we
   poll). */

#define USART0_STAT  (*(volatile uint32_t *)0x40013800U)
#define USART0_DATA  (*(volatile uint32_t *)0x40013804U)
#define USART0_BAUD  (*(volatile uint32_t *)0x40013808U)
#define USART0_CTL0  (*(volatile uint32_t *)0x4001380CU)

#define STAT_TC    (1U << 6)
#define STAT_TXE   (1U << 7)
#define STAT_RBNE  (1U << 5)
#define STAT_ORERR (1U << 3)
#define STAT_FERR  (1U << 1)
#define CTL0_UEN   (1U << 13)
#define CTL0_RBNEIE (1U << 5)

#define CLOCK_HZ 48000000U

/* Received bytes go into a ring buffer from the interrupt: the receive register holds one byte
   (174 us at 57600 baud), and the main loop is often busier than that (LCD drawing, retuning), which
   lost command characters while bytes were polled. */
static volatile uint8_t rx_buf[256];
static volatile uint8_t rx_head, rx_tail;
static volatile uint32_t rx_count;

void debug_init(uint32_t baud)
{
    uint32_t t = 1000000;
    while (!(USART0_STAT & STAT_TC) && --t) {}     /* let anything in flight finish */
    USART0_CTL0 &= ~CTL0_UEN;
    USART0_BAUD = (CLOCK_HZ + baud / 2) / baud;    /* 48 MHz / 57600 = 833 */
    USART0_CTL0 |= CTL0_RBNEIE | CTL0_UEN;
    NVIC_EnableIRQ(USART0_IRQn);                    /* runs once interrupts are enabled */
}

void USART0_IRQHandler(void)
{
    uint32_t st = USART0_STAT;
    if (st & (STAT_RBNE | STAT_ORERR)) {
        uint8_t c = (uint8_t)USART0_DATA;          /* reading STAT then DATA clears the flags */
        if (!(st & STAT_RBNE) || (c == 0 && (st & STAT_FERR)))
            return;                                 /* an overrun, or a break: PTT pulls RX low */
        uint8_t next = (uint8_t)(rx_head + 1);
        if (next != rx_tail) {
            rx_buf[rx_head] = c;
            rx_head = next;
        }
        rx_count++;
    }
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
    if (rx_tail == rx_head)
        return -1;
    int c = rx_buf[rx_tail];
    rx_tail = (uint8_t)(rx_tail + 1);
    return c;
}

uint32_t debug_rx_count(void)
{
    return rx_count;
}

void debug_write(const uint8_t *p, uint32_t n)
{
    while (n--)
        debug_putc((char)*p++);
}

/* Log output (debug_puts / debug_hex*) is muted while a PC is talking to us in programming mode,
   so it can't land among the protocol bytes; debug_putc / debug_write always send. */
static uint8_t muted;

void debug_mute(int on)
{
    muted = (uint8_t)on;
}

void debug_puts(const char *s)
{
    if (muted)
        return;
    while (*s)
        debug_putc(*s++);
}

static const char hex_chars[] = "0123456789ABCDEF";

void debug_hex32(uint32_t v)
{
    if (muted)
        return;
    for (int i = 28; i >= 0; i -= 4)
        debug_putc(hex_chars[(v >> i) & 0xF]);
}

void debug_hex16(uint16_t v)
{
    if (muted)
        return;
    for (int i = 12; i >= 0; i -= 4)
        debug_putc(hex_chars[(v >> i) & 0xF]);
}
