#ifndef RADIO_H
#define RADIO_H

#include <stdint.h>

/* Receive core: tuning, squelch from the radio's own calibration, audio, search. */
void radio_init(void);                      /* loads calibration and the squelch level setting */
void radio_tune(uint32_t freq_10hz, int am, int narrow);
uint32_t radio_freq(void);
void radio_set_squelch(int level);          /* 0 = always open, 1-9 as stock */
int  radio_squelch(void);
void radio_set_monitor(int on);
int  radio_monitor(void);
int  radio_poll(void);                      /* every 20 ms; 1 when the audio opened or closed */
int  radio_open(void);
int  radio_rssi_dbm(void);
void radio_mute(void);                      /* audio off until the next radio_tune() */

void radio_search_start(void);
int  radio_search_poll(uint32_t *found);    /* every 50 ms; 1 with the frequency when settled */
void radio_search_stop(void);               /* back to receiving on the tuned frequency */

#endif
