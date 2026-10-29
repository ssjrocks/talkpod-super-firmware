#ifndef DEBUG_H
#define DEBUG_H

#include <stdint.h>

void debug_init(void);
void debug_putc(char c);
void debug_puts(const char *s);
void debug_hex32(uint32_t v);
void debug_hex16(uint16_t v);

#endif
