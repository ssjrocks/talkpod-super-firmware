#ifndef WATCH_H
#define WATCH_H

#include <stdint.h>
#include "channels.h"

/* Two receive slots, A and B, each a channel or a VFO frequency. With dual watch on, the receiver
   alternates between them and stays on one while it has a signal. */
typedef struct {
    uint8_t is_channel;
    uint16_t channel;           /* 1..512 */
    uint32_t vfo;               /* 10 Hz units */
    uint8_t vfo_am, vfo_narrow;
    uint8_t low_power;          /* TX power for this slot; set from the channel when it changes */
    /* resolved by watch_update() */
    uint32_t freq;
    uint32_t tx;                /* 0: transmit off on this channel */
    tone_t rx_tone, tx_tone;
    uint8_t am, narrow, bcl, scramble;
    uint8_t pttid, scode;           /* stock's PTT-ID: 1 at key-up, 2 at key-down; its DTMF code group */
    uint8_t reverse;                /* receive on the TX frequency and transmit on the RX one */
    char name[13];
} slot_t;

enum { WATCH_EV_NONE, WATCH_EV_OPEN, WATCH_EV_CLOSE };

void    watch_init(void);
slot_t *watch_slot(int i);
int     watch_selected(void);
void    watch_select(int i);
void    watch_set_dual(int on);
int     watch_dual(void);
void    watch_update(int i);    /* after changing a slot: resolve it and retune */
int     watch_rx_slot(void);    /* the slot the receiver is on now */
void    watch_pause(int on);    /* scan and search take the receiver meanwhile */
int     watch_poll(void);       /* every 10 ms; a WATCH_EV_* for the slot watch_rx_slot() */

#endif
