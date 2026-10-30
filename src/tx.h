#ifndef TX_H
#define TX_H

#include <stdint.h>

/* Transmit. Never in 108-138 MHz (aviation) or 406.0-406.1 MHz (distress beacons): the user's
   licence conditions, see docs. Also not where the BK4819 cannot synthesise (660-840 MHz). */
void     tx_init(void);                 /* reads the PA calibration from flash */
int      tx_allowed(uint32_t freq_10hz);
int      tx_start(uint32_t freq_10hz, int narrow, int low_power);    /* 0 if not allowed */
void     tx_stop(void);                 /* PA off, transmitter off, RF path off; then retune to RX */
int      tx_active(void);
uint32_t tx_seconds(void);
uint16_t tx_power_reg(uint32_t freq_10hz, int low_power);           /* REG_36 for this frequency */

#endif
