#ifndef CHANNELS_H
#define CHANNELS_H

#include <stdint.h>
#include "bk4819.h"

/* The stock channel memory in the external flash: 512 records of 32 bytes from 0x0000, the layout
   CHIRP's mml_jc8810 driver uses (the PC writes it through program mode, src/progmode.c). */
#define CHANNEL_COUNT 512

enum { TONE_NONE = BK_TONE_NONE, TONE_CTCSS = BK_TONE_CTCSS, TONE_DCS = BK_TONE_DCS,
       TONE_DCS_INV = BK_TONE_DCS_INV };

typedef struct {
    uint8_t type;                   /* TONE_* */
    uint16_t value;                 /* CTCSS: Hz x 10; DCS: the 9-bit code (023 octal = 0x13) */
} tone_t;

typedef struct {
    uint32_t rx;                    /* 10 Hz units */
    uint32_t tx;                    /* 0: transmit off */
    tone_t rx_tone, tx_tone;
    uint8_t narrow, scan, bcl;
    uint8_t low_power;              /* stock: 0 = high, 1 = low, 2 = medium (shown as high) */
    uint8_t scramble;               /* 0 off, 1-2 (stored as 4 / 8, CHIRP's SCRAM1 / SCRAM2) */
    uint8_t encrypt;                /* 0 off, 1-3 (stock "ENCRY") */
    uint8_t pttid, scode;           /* PTT-ID: 0 off, 1 BOT, 2 EOT, 3 both; DTMF code group 0-14 */
    char name[13];
} channel_t;

int channel_read(int n, channel_t *ch);     /* n = 1..512; 0 if the slot is empty */
int channel_next(int n, int dir, int scan_only);   /* next used slot after n (wraps), 0 if none */
tone_t tone_decode(uint16_t raw);           /* a channel's stored tone (CHIRP's encoding) */

#endif
