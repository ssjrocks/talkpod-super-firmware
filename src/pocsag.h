#ifndef POCSAG_H
#define POCSAG_H

#include <stdint.h>

/* POCSAG paging (see pocsag.c). */
typedef struct __attribute__((packed)) {
    uint32_t ric;                       /* where MENU sends when no received page is picked */
    uint8_t speed;                      /* 0 512, 1 1200, 2 2400 bit/s */
    uint8_t invert;                     /* send with the bits inverted */
} pager_cfg_t;

typedef struct {
    char from[10];                      /* the RIC */
    char text[48];
    uint8_t flags;                      /* PAGE_OUT: one we sent */
} page_t;

enum { PAGE_OUT = 1, PAGE_NUMERIC = 2 };
enum { PAGE_EV_NEW = 1, PAGE_EV_RX = 2 };

extern pager_cfg_t pager_cfg;
extern const uint16_t pager_bauds[3];

void pocsag_init(void);
void pocsag_save(void);
void pocsag_apply(void);                /* after a change to the speed or polarity */
void pocsag_listen(int on);             /* the receiver's modem: POCSAG, or back to UV-K5 messages */
int  pocsag_poll(void);                 /* every 10 ms while listening and not transmitting */
int  pocsag_receiving(void);
int  pocsag_send(uint32_t ric, const char *text, uint32_t freq, int narrow, int low_power);
                                        /* 1 sent, 0 failed, -1 not allowed on this frequency */
int  pocsag_count(void);
const page_t *pocsag_get(int i);        /* 0 = newest */

#endif
