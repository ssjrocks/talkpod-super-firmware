#include "channels.h"
#include "spiflash.h"

/* record: lbcd rxfreq[4], lbcd txfreq[4] (all 0xFF: transmit off), ul16 rxtone, ul16 txtone,
   u8 signalling code (low 4 bits), u8 pttid (low 2 bits), u8 scramble << 2 | power (0 high,
   1 low, 2 medium), u8 flags (bit 6 narrow, bits 5-4 encryption, bit 3 busy lockout, bit 2 scan),
   lbcd code[3], u8, char name[12]

   Tones (CHIRP's encoding): 0 or 0xFFFF none; 600 and up CTCSS in 0.1 Hz; 1-0x69 the DCS code at
   that place in DCS_CODES (+1); 0x6A and up the same, inverted. */
static const uint16_t DCS_CODES[104] = {
    0x013, 0x015, 0x016, 0x019, 0x01A, 0x01E, 0x023, 0x027, 0x029, 0x02B, 0x02C, 0x035, 0x039,
    0x03A, 0x03B, 0x03C, 0x04C, 0x04D, 0x04E, 0x052, 0x055, 0x059, 0x05A, 0x05C, 0x063, 0x065,
    0x06A, 0x06D, 0x06E, 0x072, 0x075, 0x07A, 0x07C, 0x085, 0x08A, 0x093, 0x095, 0x096, 0x0A3,
    0x0A4, 0x0A5, 0x0A6, 0x0A9, 0x0AA, 0x0AD, 0x0B1, 0x0B3, 0x0B5, 0x0B6, 0x0B9, 0x0BC, 0x0C6,
    0x0C9, 0x0CD, 0x0D5, 0x0D9, 0x0DA, 0x0E3, 0x0E6, 0x0E9, 0x0EE, 0x0F4, 0x0F5, 0x0F9, 0x109,
    0x10A, 0x10B, 0x113, 0x119, 0x11A, 0x125, 0x126, 0x12A, 0x12C, 0x12D, 0x132, 0x134, 0x135,
    0x136, 0x143, 0x146, 0x14E, 0x153, 0x156, 0x15A, 0x166, 0x175, 0x186, 0x18A, 0x194, 0x197,
    0x199, 0x19A, 0x1AC, 0x1B2, 0x1B4, 0x1C3, 0x1CA, 0x1D3, 0x1D9, 0x1DA, 0x1DC, 0x1E3, 0x1EC,
};

tone_t tone_decode(uint16_t raw)
{
    tone_t t = {TONE_NONE, 0};
    if (raw == 0 || raw == 0xFFFF) {
        return t;
    } else if (raw >= 600) {
        t.type = TONE_CTCSS;
        t.value = raw;
    } else if (raw <= 0x69) {
        t.type = TONE_DCS;
        t.value = raw - 1 < 104 ? DCS_CODES[raw - 1] : 0;
    } else {
        t.type = TONE_DCS_INV;
        t.value = raw - 0x6A < 104 ? DCS_CODES[raw - 0x6A] : 0;
    }
    if (t.type != TONE_CTCSS && !t.value)
        t.type = TONE_NONE;
    return t;
}
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
    ch->tx = lbcd(r + 4);
    if (ch->tx < 1800000 || ch->tx > 130000000)     /* 0xFF...: transmit off */
        ch->tx = 0;
    ch->rx_tone = tone_decode((uint16_t)(r[8] | r[9] << 8));
    ch->tx_tone = tone_decode((uint16_t)(r[10] | r[11] << 8));
    ch->scode = r[12] & 0x0F;
    ch->pttid = r[13] & 3;
    uint8_t s = (uint8_t)((r[14] >> 2) & 0x3F);
    ch->scramble = s == SCRAMBLE_ROLL_STORED ? BK_SCRAMBLE_ROLL
                 : s % 4 == 0 && s <= 40 ? s / 4 : 1;    /* 4 x type; 0 off; anything else type 1 */
    ch->low_power = (r[14] & 3) == 1;
    ch->narrow = (r[15] >> 6) & 1;
    ch->encrypt = (r[15] >> 4) & 3;
    if (ch->encrypt) {                          /* stock's ENCRY takes the place of no tone or CTCSS */
        tone_t *t[2] = {&ch->rx_tone, &ch->tx_tone};
        for (int i = 0; i < 2; i++)
            if (t[i]->type == TONE_NONE || t[i]->type == TONE_CTCSS) {
                t[i]->value = (uint16_t)(ch->encrypt << 12 | (t[i]->type == TONE_CTCSS ? t[i]->value : 0));
                t[i]->type = TONE_ENCRY;
            }
    }
    ch->bcl = (r[15] >> 3) & 1;
    ch->scan = (r[15] >> 2) & 1;
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
