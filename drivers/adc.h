#ifndef ADC_H
#define ADC_H

#include <stdint.h>

void     adc_init(void);
uint16_t adc_read(int channel);        /* 12-bit; channel 0 = PA0 (mic level), 1 = PA1 (battery) */
int      battery_decivolts(void);      /* stock scale: (reading >> 4) * 482 / 1000 tenths of a volt */
int      battery_bars(void);           /* 0-5, from this radio's thresholds (flash 0xF200) */

#endif
