#include "channels.h"
#include "spiflash.h"

/* record: lbcd rxfreq[4], lbcd txfreq[4], ul16 rxtone, ul16 txtone, u8 signalling, u8 pttid,
   u8 scramble << 2 | power (0 high, 1 low, 2 medium), u8 flags (bit 6 narrow, bit 2 scan),
   lbcd code[3], u8, char name[12] */
static uint32_t lbcd(const uint8_t *p)
{
    uint32_t v = 0;
    for (int i = 3; i >= 0; i--) {
        if ((p[i] >> 4) > 9 || (p[i] & 15) > 9)
            return 0;
        v = v * 100 + (uint32_t)(p[i] >> 4) * 10 + (p[i] & 15);
    }
    return v;
}

int channel_read(int n, channel_t *ch)
{
    uint8_t r[32];
    if (n < 1 || n > CHANNEL_COUNT)
        return 0;
    spiflash_read((uint32_t)(n - 1) * 32, r, sizeof r);
    ch->rx = lbcd(r);
    if (ch->rx < 1800000 || ch->rx > 130000000)     /* empty (0xFF) or out of any range */
        return 0;
    ch->narrow = (r[15] >> 6) & 1;
    ch->scan = (r[15] >> 2) & 1;
    ch->low_power = (r[14] & 3) == 1;
    int len = 0;
    for (int i = 0; i < 12 && r[20 + i] >= 0x20 && r[20 + i] < 0x7F; i++)
        ch->name[len++] = (char)r[20 + i];
    while (len && ch->name[len - 1] == ' ')
        len--;
    ch->name[len] = 0;
    return 1;
}

int channel_next(int n, int dir, int scan_only)
{
    channel_t ch;
    for (int i = 0; i < CHANNEL_COUNT; i++) {
        n += dir;
        if (n < 1)
            n = CHANNEL_COUNT;
        if (n > CHANNEL_COUNT)
            n = 1;
        if (channel_read(n, &ch) && (!scan_only || ch.scan))
            return n;
    }
    return 0;
}
