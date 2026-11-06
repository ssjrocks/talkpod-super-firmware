#ifndef EVLOG_H
#define EVLOG_H

#include <stdint.h>

/* What the receiver did, the last EVLOG_N events in RAM (src/evlog.c). Plugging in the cable doesn't
   restart the radio, so the log of a problem is still there: the shell's 'L' prints it, 'l' clears
   it, and tools/eventlog.py reads and explains it. */
enum {
    EV_BOOT = 1,
    EV_TUNE,            /* a: modulation | narrow << 4, v: frequency (10 Hz) */
    EV_OPEN,            /* a: RSSI dBm + 160, v: frequency */
    EV_CLOSE,           /* the same */
    EV_HOLD,            /* a: the HOLD_* bits now, v: frequency */
    EV_IRQ,             /* v: BK4819 REG_02 flags (FIFO-almost-full left out) */
    EV_PKT,             /* a: the packet's type byte */
    EV_PKT_LOST,        /* a sync word, then no complete packet */
    EV_TX,              /* a: 1 on, 0 off, v: frequency */
    EV_SQUELCH,         /* a: level */
    EV_CARRIER,         /* a: 1 found, 0 gone, while the squelch stays as it was (the tone isn't there) */
};

void evlog(uint8_t ev, uint8_t a, uint32_t v);
void evlog_dump(void);
void evlog_clear(void);

#endif
