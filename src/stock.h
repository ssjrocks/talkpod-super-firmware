#ifndef STOCK_H
#define STOCK_H

#include <stdint.h>

/* The stock firmware's settings (src/stock.c), as program mode shows them at 0x9000. Offsets and
   values as CHIRP's mml_jc8810 driver; this firmware uses the same record, so CHIRP, Talkpod Manager
   and a return to stock firmware all see one set of settings. */
typedef struct __attribute__((packed)) {
    uint8_t sql, save, vox, abr, tdr, tot, beep, voice;                 /* 0x00 */
    uint8_t language, dtmfst, screv, pttid, pttlt, mdfa, mdfb, x0f;     /* 0x08 */
    uint8_t autolk, almode, alarmsound, dualtx, ste, rpste, rptrl, roger; /* 0x10 */
    uint8_t x18, fmradio, vfomr, kblock, ponmsg, x1d, tone, x1f;        /* 0x18 */
    uint8_t voxd, menuquit, tailcode, x23, unlock_sw_ch, x25, x26, x27; /* 0x20 */
    uint8_t x28, qtsave, ani, skey2_sp, skey2_lp, skey3_sp, topkey_sp, tailphase; /* 0x28 */
    uint8_t rxendtail;                                                  /* 0x30 */
    uint8_t rest[15];
} stock_t;

#define STOCK_BLOCK 0x40                /* what program mode shows of a record */
#define STOCK_VFO   0x8000U
#define STOCK_SET   0x9000U

extern stock_t stock;

void stock_load(void);                  /* the newest record, or stock's defaults */
int  stock_save(void);                  /* a new record, if stock changed; 1 unless flash failed */
void stock_block_read(uint32_t base, uint8_t *out);          /* STOCK_VFO / STOCK_SET, STOCK_BLOCK bytes */
int  stock_block_write(uint32_t base, const uint8_t *data);  /* as stock's program mode: a new record */

#endif
