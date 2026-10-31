#ifndef BK1080_H
#define BK1080_H

#include <stdint.h>

/* The FM broadcast receiver. Frequencies in 0.1 MHz, 650 (65.0 MHz) to 1080 (108.0 MHz). */
void bk1080_on(void);
void bk1080_off(void);
void bk1080_tune(uint16_t tenths);

#endif
