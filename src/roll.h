#ifndef ROLL_H
#define ROLL_H

#include <stdint.h>

/* The rolling scrambler (src/roll.c): scramble "Rolling" on a channel. */
typedef struct __attribute__((packed)) {
    char key[15];                       /* shared by the radios that talk on rolling channels */
    uint32_t scan_lo, scan_hi;          /* the frequency scanner's range (10 Hz units), kept here too */
    uint8_t scan_step;                  /* an index into ui.c's steps[] */
    uint16_t fm;                        /* the FM radio's frequency, 0.1 MHz */
} roll_cfg_t;
extern roll_cfg_t roll_cfg;

void roll_init(void);                   /* the key from flash */
void roll_save(void);
int  roll_tx(int narrow);               /* just after tx_start(): send the burst, then glide; 1 if sent */
int  roll_rx(const uint8_t *burst, uint32_t end_ms);  /* a burst heard, its data ending at end_ms:
                                                         1 gliding along, -1 another key, 0 not a burst */
void roll_poll(void);                   /* as often as possible: follows the path, ends when the
                                           transmission does */
void roll_stop(void);
int  roll_active(void);

#endif
