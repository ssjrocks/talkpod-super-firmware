#ifndef TUNE_H
#define TUNE_H

#include <stdint.h>

/* Short tunes on the BK4819's tone generator, through the speaker or on the air (src/tune.c) */
int  tune_count(void);
const char *tune_name(int i);
uint32_t tune_ms(int i, int on_air);    /* how long playing it takes; on the air with the carrier around it */
int  tune_start(int i, uint32_t freq, int narrow, int low_power, int tone, uint16_t tone_value);
                                        /* freq 0: the speaker. 1 playing, -1 TX not allowed there */
void tune_poll(void);                   /* as often as possible */
void tune_stop(void);                   /* now */
int  tune_active(void);

#endif
