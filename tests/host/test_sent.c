/* src/sent.c on the PC, against a simulated SPI flash (erase sets 0xFF, a write can only clear bits),
 * run with `make host-test`: the newest text, the count, sector rotation, duplicates, long texts and a
 * damaged record. */
#include <stdio.h>
#include <string.h>
#include "../../src/sent.h"
#include "../../drivers/spiflash.h"

/* a failed check prints and exits: an abort() would make a core dump (and apport) */
#define assert(x) do { if (!(x)) { printf("FAILED line %d: %s (i = %d)\n", __LINE__, #x, dbg_i); return 1; } } while (0)
static int dbg_i = -1;

static uint8_t flash[0x200000];
static int erases, writes;

void spiflash_read(uint32_t addr, void *dst, uint32_t len) { memcpy(dst, flash + addr, len); }
int spiflash_erase(uint32_t addr)
{
    memset(flash + (addr & ~(FLASH_SECTOR - 1)), 0xFF, FLASH_SECTOR);
    erases++;
    return 1;
}
int spiflash_write(uint32_t addr, const void *src, uint32_t len)
{
    const uint8_t *p = src;
    for (uint32_t i = 0; i < len; i++)
        flash[addr + i] &= p[i];
    writes++;
    return 1;
}
uint16_t crc16(const uint8_t *p, uint32_t n)
{
    uint16_t crc = 0;
    while (n--) {
        crc ^= (uint16_t)(*p++ << 8);
        for (int i = 0; i < 8; i++)
            crc = (uint16_t)(crc & 0x8000 ? crc << 1 ^ 0x1021 : crc << 1);
    }
    return crc;
}

#include "../../src/sent.c"

int main(void)
{
    char t[SENT_TEXT + 1], want[SENT_TEXT + 8];
    int k;
    memset(flash, 0xFF, sizeof flash);
    assert(sent_count() == 0 && !sent_get(0, t, &k));

    for (int i = 0; i < 1000; i++) {                    /* 40 rotations of a sector */
        dbg_i = i;
        snprintf(want, sizeof want, "message %d", i);
        sent_add(want, i % 4);
        assert(sent_get(0, t, &k) && !strcmp(t, want) && k == i % 4);
        int n = sent_count();
        assert(n >= (i + 1 < 25 ? i + 1 : 25) && n <= 50);
        for (int j = 0; j < n; j++) {                   /* every kept one, in order */
            snprintf(want, sizeof want, "message %d", i - j);
            assert(sent_get(j, t, &k) && !strcmp(t, want));
        }
    }
    printf("erases %d writes %d\n", erases, writes);
    assert(erases == 39 && writes == 1000);              /* a switch every 25, the first sector blank */

    int n = sent_count();
    sent_add("message 999", 0);                         /* the newest again: not kept twice */
    assert(sent_count() == n && writes == 1000);

    memset(want, 'X', 150);
    want[150] = 0;
    sent_add(want, SENT_MORSE);                         /* cut to SENT_TEXT */
    assert(sent_get(0, t, &k) && strlen(t) == SENT_TEXT && k == SENT_MORSE);

    int a, b, cur = current(&a, &b);                    /* a record cut short by power loss */
    flash[at(cur, a - 1) + 10] ^= 0x01;
    assert(sent_get(0, t, &k) && !strcmp(t, "?"));
    assert(sent_get(1, t, &k) && !strcmp(t, "message 999"));

    printf("sent.c: 1000 texts, %d sector erases, newest and order right at every step; tie, duplicate, long text and damaged record right\n", erases);
    return 0;
}
