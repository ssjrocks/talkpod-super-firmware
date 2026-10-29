#ifndef SYSTEM_H
#define SYSTEM_H

#include <stdint.h>

void systick_init(void);
uint32_t millis(void);
void delay_ms(uint32_t ms);
void fwdgt_feed(void);
void system_reset(void) __attribute__((noreturn));   /* NVIC reset (the bootloader runs again) */

#endif
