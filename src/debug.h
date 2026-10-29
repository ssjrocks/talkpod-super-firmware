#ifndef DEBUG_H
#define DEBUG_H

#include <stdint.h>

void debug_init(uint32_t baud);
void debug_putc(char c);
void debug_flush(void);
int  debug_getc(void);                 /* -1 if nothing received */
uint32_t debug_rx_count(void);         /* bytes received so far, breaks excluded */
void debug_write(const uint8_t *p, uint32_t n);
void debug_mute(int on);               /* mutes debug_puts / debug_hex* (progmode.c) */
void debug_puts(const char *s);
void debug_hex32(uint32_t v);
void debug_hex16(uint16_t v);

#endif
