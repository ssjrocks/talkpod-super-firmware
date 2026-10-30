#ifndef VOICE_H
#define VOICE_H

#include <stdint.h>

/* Sounds, numbered as the stock voice prompts (tools/soundpack.py keeps only these). */
enum {
    SND_LOW_VOLTAGE = 120,
    SND_CHANNEL_MODE = 121,
    SND_FREQUENCY_MODE = 122,
    SND_MENU = 137,
    SND_HIGH = 138,
    SND_LOW = 139,
    SND_KEY = 149,
    SND_CANCEL = 151,
    SND_POWER_ON = 153,
    SND_OPEN = 156,
};

int  voice_play(int id);        /* stops any sound playing; 0 if the pack has no such sound */
int  voice_busy(void);
void voice_debug(uint32_t v[8]);    /* state, interrupts, rd, wr, left, C4DT, CTRL1|BRK<<8, CNT */
void voice_poll(void);          /* main loop: refills the buffer from flash, finishes playback */

#endif
