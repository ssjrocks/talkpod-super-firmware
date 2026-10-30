#ifndef BK4819_H
#define BK4819_H

#include <stdint.h>

enum { BK_AF_MUTE, BK_AF_FM, BK_AF_AM };

void     bk4819_init(void);                     /* GPIO + the stock register init */
void     bk4819_write(uint8_t reg, uint16_t val);
uint16_t bk4819_read(uint8_t reg);
void     bk4819_rx(uint32_t freq_10hz, int narrow, uint16_t reg78, uint16_t reg4f);
                                                /* stock receive start (no tone squelch) */
void     bk4819_off(void);                      /* REG_30 = 0: receiver and transmitter off */
void     bk4819_tx(uint32_t freq_10hz, int narrow, uint16_t reg36);   /* transmit on */
void     bk4819_pa_off(void);
void     bk4819_af(int type);                   /* audio output: mute, FM or AM */
int      bk4819_rssi_dbm(void);
void     bk4819_gpio(uint8_t mask, int on);     /* the BK4819's own GPIO pins (REG_33) */
void     bk4819_beep(int on, uint16_t hz);      /* local tone; after it, call bk4819_rx() again */
int      bk4819_squelch_open(void);             /* the chip's squelch decision (REG_0C bit 1) */
void     bk4819_search(int on);                 /* frequency counter on / off */
int      bk4819_search_result(uint32_t *freq_10hz);
uint16_t bk4819_irq(void);                      /* pending interrupt flags (REG_02), cleared; 0 if none */

/* FSK modem for text messages (UV-K5 messenger settings). Once set up it listens on every frequency
   bk4819_rx() tunes. Flags in bk4819_irq(): 0x0002 sync found, 0x1000 a word arrived, 0x2000 the
   packet ended, 0x8000 sent. */
enum { BK_FSK_1200, BK_FSK_700, BK_FSK_450, BK_FSK_OFF = 0xFF };
void     bk4819_fsk_setup(int modem, int rx_len);
int      bk4819_fsk_send(const uint8_t *data, int len, int narrow);  /* transmitter on; blocks; 1 if sent */
uint16_t bk4819_fsk_word(void);                 /* the next two received bytes, first in the low byte */
void     bk4819_fsk_rearm(void);                /* clear the FIFO and listen again */

#endif
