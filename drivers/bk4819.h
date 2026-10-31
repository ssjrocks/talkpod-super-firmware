#ifndef BK4819_H
#define BK4819_H

#include <stdint.h>

enum { BK_AF_MUTE, BK_AF_FM, BK_AF_AM };

void     bk4819_init(void);                     /* GPIO + the stock register init */
void     bk4819_write(uint8_t reg, uint16_t val);
uint16_t bk4819_read(uint8_t reg);
enum { BK_TONE_NONE, BK_TONE_CTCSS, BK_TONE_DCS, BK_TONE_DCS_INV, BK_TONE_ENCRY };   /* tone value: Hz x 10 / 9-bit
                                                   DCS code / stock's ENCRY << 12 | the channel's CTCSS */
enum { BK_SCRAMBLE_ROLL = 11 };                 /* scramble: 0 off, 1-10 the UV-K5's types, or rolling */
void     bk4819_rx(uint32_t freq_10hz, int narrow, uint16_t reg78, uint16_t reg4f, int tone, uint16_t tone_value,
                   int scramble);               /* stock receive start */
void     bk4819_scramble_hz(uint16_t hz);       /* the inversion frequency, now; 0 scrambler off */
void     bk4819_cal(uint8_t mic, uint8_t ctcss, uint8_t dcs);   /* the radio's calibration (flash 0xF210) */
void     bk4819_off(void);                      /* REG_30 = 0: receiver and transmitter off */
void     bk4819_tx(uint32_t freq_10hz, int narrow, uint16_t reg36, int tone, uint16_t tone_value,
                   int scramble);               /* transmit on */
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
enum { BK_FSK_1200, BK_FSK_700, BK_FSK_450, BK_FSK_AFSK, BK_FSK_POCSAG, BK_FSK_OFF = 0xFF };
void     bk4819_fsk_setup(int modem, int rx_len);
int      bk4819_fsk_send(const uint8_t *data, int len, int narrow);  /* transmitter on; blocks; 1 if sent;
                                                                       len even, at most 255 */
#define  BK_ROLL_LEN 8                          /* the rolling scrambler's burst */
int      bk4819_fsk_send_roll(const uint8_t *data, int len, int narrow, int modem);   /* own sync word */
void     bk4819_fsk_voice(void);                /* still transmitting after a send: modem off, mic on */
uint32_t bk4819_fsk_sent_ms(void);              /* millis() when the last send finished */
int      bk4819_fsk_roll_rx(void);              /* 1: listening for the rolling burst, not messages */
uint16_t bk4819_fsk_word(void);                 /* the next two received bytes, first in the low byte */
void     bk4819_fsk_rearm(void);                /* clear the FIFO and listen again */
void     bk4819_fsk_invert(int on);             /* receive with the bits inverted (BK_FSK_AFSK) */
void     bk4819_afsk_rx_mode(uint16_t r58);
void     bk4819_fsk_tx_invert(int on);          /* send the bits inverted */
void     bk4819_pocsag_baud(uint16_t baud);     /* BK_FSK_POCSAG: 512, 1200 or 2400 */     /* REG_58 for BK_FSK_AFSK, to try demodulator modes */
void     bk4819_tone_tx(int on);                /* while transmitting: tone 1 instead of the mic */
void     bk4819_tone_hz(uint16_t hz);
void     bk4819_dtmf(int digit);                /* with bk4819_tone_tx(1): a DTMF digit, -1 silence */
void     bk4819_tail(int dcs, int phase);       /* while transmitting: the squelch tail */
void     bk4819_roger_data(void);               /* while transmitting: roger TONE1200 */

#endif
