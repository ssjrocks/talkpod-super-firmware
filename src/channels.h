#ifndef CHANNELS_H
#define CHANNELS_H

#include <stdint.h>

/* The stock channel memory in the external flash (read-only): 512 records of 32 bytes from 0x0000,
   the layout CHIRP's mml_jc8810 driver uses. */
#define CHANNEL_COUNT 512

typedef struct {
    uint32_t rx;                    /* 10 Hz units */
    uint8_t narrow, scan;
    char name[13];
} channel_t;

int channel_read(int n, channel_t *ch);     /* n = 1..512; 0 if the slot is empty */
int channel_next(int n, int dir, int scan_only);   /* next used slot after n (wraps), 0 if none */

#endif
