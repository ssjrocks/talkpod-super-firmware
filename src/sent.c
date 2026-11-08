/* The texts sent from Messages, APRS, the pager and Morse, for the typing screens' history (hold FM).
 * They are in the external flash at FLASH_SENT, so they outlive power-off: two 4 KB sectors written in
 * turn, records of REC bytes {u16 magic, u8 length, u8 kind, the text, zeros, u16 sequence, u16
 * CRC-16/XMODEM}. When the sector being written is full, the other one is erased and written next, so
 * the newest PER records (25) or more are always there; while both are full, the sequence number tells
 * which is newer. A record cut short by power loss fails its CRC: it reads as
 * "?". */
#include <string.h>
#include "sent.h"
#include "settings.h"
#include "spiflash.h"

#define MAGIC 0x5E47
#define REC   160
#define PER   ((int)(FLASH_SECTOR / REC))
#define SEQ   (REC - 4)

static uint32_t at(int sector, int slot)
{
    return FLASH_SENT + (uint32_t)sector * FLASH_SECTOR + (uint32_t)slot * REC;
}

static int used(int sector)             /* records written in a sector */
{
    uint8_t h[2];
    for (int i = 0; i < PER; i++) {
        spiflash_read(at(sector, i), h, sizeof h);
        if (h[0] == 0xFF && h[1] == 0xFF)
            return i;
    }
    return PER;
}

static uint16_t seq(int sector, int slot)
{
    uint8_t s[2];
    spiflash_read(at(sector, slot) + SEQ, s, sizeof s);
    return (uint16_t)(s[0] | s[1] << 8);
}

/* the sector being written: the one that isn't full; while both are full, the newer; the first while
   both are empty */
static int current(int *n_cur, int *n_other)
{
    int u0 = used(0), u1 = used(1);
    int cur = u0 == PER && u1 == PER ? (int16_t)(seq(1, PER - 1) - seq(0, PER - 1)) > 0
                                     : u1 && (u1 < PER || u0 == PER);
    *n_cur = cur ? u1 : u0;
    *n_other = cur ? u0 : u1;
    return cur;
}

int sent_count(void)
{
    int a, b;
    current(&a, &b);
    return a + b;
}

int sent_get(int i, char *text, int *kind)
{
    int a, b, cur = current(&a, &b);
    uint8_t r[REC];
    if (i < 0 || i >= a + b)
        return 0;
    spiflash_read(i < a ? at(cur, a - 1 - i) : at(!cur, b - 1 - (i - a)), r, REC);
    int n = r[2];
    if ((r[0] | r[1] << 8) != MAGIC || n > SENT_TEXT || crc16(r, REC - 2) != (r[REC - 2] | r[REC - 1] << 8))
        n = 0;
    memcpy(text, r + 4, (size_t)n);
    text[n] = 0;
    if (!n)
        strcpy(text, "?");
    *kind = r[3];
    return 1;
}

void sent_add(const char *text, int kind)
{
    uint8_t r[REC];
    int a, b, cur, n = (int)strlen(text);
    if (!n)
        return;
    if (n > SENT_TEXT)
        n = SENT_TEXT;
    char *newest = (char *)r;                   /* the record buffer, before it's built */
    int k;
    if (sent_get(0, newest, &k) && (int)strlen(newest) == n && !memcmp(newest, text, (size_t)n))
        return;
    cur = current(&a, &b);
    uint16_t s = a ? seq(cur, a - 1) + 1 : b ? seq(!cur, b - 1) + 1 : 0;
    memset(r, 0, sizeof r);
    r[SEQ] = (uint8_t)s;
    r[SEQ + 1] = (uint8_t)(s >> 8);
    r[0] = MAGIC & 0xFF;
    r[1] = MAGIC >> 8;
    r[2] = (uint8_t)n;
    r[3] = (uint8_t)kind;
    memcpy(r + 4, text, (size_t)n);
    uint16_t c = crc16(r, REC - 2);
    r[REC - 2] = (uint8_t)c;
    r[REC - 1] = (uint8_t)(c >> 8);
    if (a == PER) {                             /* full: the other sector, erased, takes the next */
        cur = !cur;
        a = 0;
        spiflash_erase(at(cur, 0));
    }
    spiflash_write(at(cur, a), r, REC);
}
