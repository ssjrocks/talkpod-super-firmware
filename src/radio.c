/* Receive core. Squelch thresholds are computed exactly as stock 0x80098f4 does, from the per-radio
 * calibration tables in the external flash (docs/stock-firmware.md), and the BK4819 makes the
 * open/close decision (REG_0C bit 1). */
#include "radio.h"
#include "bk4819.h"
#include "board.h"
#include "spiflash.h"
#include "evlog.h"
#include "system.h"

static uint8_t sq_open[10], sq_close[10];   /* RSSI per squelch level (0xF0C0, 0xF0D0) */
static uint8_t corr_400[16], corr_130[16], corr_200[16], corr_300[16];  /* per-frequency offsets */
static uint8_t noise[16];                   /* 0xF210: [3] / [4] receive audio wide / narrow,
                                               [5 + band] noise threshold, [9] hysteresis */

static uint32_t freq = 47690000;
static uint8_t am, narrow, level = 1, monitor, open, muted, hold, carrier;
static uint8_t tone, scramble;
static uint16_t tone_value;
static int tone_heard(void);

void radio_init(void)
{
    uint8_t settings;
    spiflash_read(0xF0C0, sq_open, sizeof sq_open);
    spiflash_read(0xF0D0, sq_close, sizeof sq_close);
    spiflash_read(0xF0E0, corr_400, sizeof corr_400);
    spiflash_read(0xF0F0, corr_130, sizeof corr_130);
    spiflash_read(0xF100, corr_200, sizeof corr_200);
    spiflash_read(0xF110, corr_300, sizeof corr_300);
    spiflash_read(0xF210, noise, sizeof noise);
    spiflash_read(0x9000, &settings, 1);    /* stock settings: squelch level in the low nibble */
    level = settings & 0x0F;
    if (level > 9)
        level = 1;
}

/* Correction table and band number for a frequency, as stock. Below 130 MHz (airband, broadcast)
   stock reads an uncalibrated byte; the 130-200 MHz table is used there instead. */
static uint8_t correction(uint32_t f, int *band)
{
    uint32_t mhz = f / 100000;
    if (mhz >= 400) {
        uint32_t i = (mhz - 400) / 10;
        *band = 0;
        return corr_400[i > 12 ? 12 : i];
    }
    if (mhz >= 300) {
        *band = 3;
        return corr_300[mhz >= 350 ? (mhz - 350) / 5 : 0];
    }
    if (mhz >= 200) {
        uint32_t i = (mhz - 200) / 5;
        *band = 2;
        return corr_200[i > 12 ? 12 : i];
    }
    uint32_t i = mhz >= 130 ? (mhz - 130) / 5 : 0;
    *band = 1;
    return corr_130[i > 9 ? 9 : i];
}

/* The chip opens the squelch with the RSSI above REG_78's open value and its noise indicator below
   REG_4F's (and few glitches). Measured on this radio (470-490 MHz, no signal): noise 70-80 in
   narrow FM, 50-79 in wide; a strong signal 10-32. The RSSI with no signal (-95..-125 dBm) is often
   above the calibrated thresholds (stock 0x80098f4: -120..-108 dBm at 476 MHz), so noise decides.
   Stock keeps one noise threshold for every level (calibration 0xF215 on: 0x39, so it closes at 57
   and opens at 55), and its level 1 muted weak stations other radios still made out. Here the noise
   threshold follows the level, from just below the no-signal floor (level 1: closes at 68, opens at
   64 in narrow FM) to strong signals only (level 9); about level 3 is stock's level 1. Wide FM sits
   WIDE_NOISE lower, its floor reaching 50. RSSI (raw units, 2 per dB): level 1 is 6 dB below stock's
   calibrated threshold, so it never keeps out a weak signal; each level above adds LEVEL_RSSI. */
static const int8_t noise_ofs[10] = {0, 11, 6, 2, -2, -6, -10, -14, -19, -24};
#define WIDE_NOISE   20
#define HYST_MIN     4
#define MIN_CLOSE    24                 /* a strong signal reads 10-32: never ask for less */
#define LEVEL_RSSI   6
static uint8_t weak_noise;              /* level 1's close threshold here: a signal at or below it */
static uint32_t tuned_at, weak_at;

static void squelch_regs(uint16_t *reg78, uint16_t *reg4f)
{
    int band;
    uint8_t c = correction(freq, &band);
    int o = sq_open[level], cl = sq_close[level];
    if (o > c) {
        o -= c;
        cl = cl > c ? cl - c : 0;
    }
    if (level == 1) {
        o = o > 12 ? o - 12 : 0;
        cl = cl > 12 ? cl - 12 : 0;
    } else if (level > 1) {
        o += (level - 1) * LEVEL_RSSI;
        cl += (level - 1) * LEVEL_RSSI;
    }
    *reg78 = (uint16_t)((o > 255 ? 255 : o) << 8 | (cl > 255 ? 255 : cl));

    int h = noise[9] > HYST_MIN ? noise[9] : HYST_MIN;
    int base = (noise[5 + band] & 0x7F) - (narrow ? 0 : WIDE_NOISE);
    int n = base + noise_ofs[level < 10 ? level : 9];
    if (n < MIN_CLOSE)
        n = MIN_CLOSE;
    weak_noise = (uint8_t)(base + noise_ofs[1]);
    *reg4f = (uint16_t)(n << 8 | (n - h));
}

static void set_open(int on)
{
    open = (uint8_t)on;
    bk4819_af(on && !hold ? (am == MOD_SSB ? BK_AF_SSB : am ? BK_AF_AM : BK_AF_FM) : BK_AF_MUTE);
    board_speaker(SPK_RADIO, on);
    board_led(0, on);
}

void radio_audio_hold(int who, int on)
{
    uint8_t was = hold;
    hold = (uint8_t)(on ? hold | who : hold & ~who);
    if (hold != was)
        evlog(EV_HOLD, hold, freq);
    set_open(open);
}

void radio_set_tone(int type, uint16_t value, int scramble_type)
{
    tone = (uint8_t)type;
    tone_value = value;
    scramble = (uint8_t)scramble_type;
}

void radio_tune(uint32_t freq_10hz, int am_mode, int narrow_bw)
{
    uint16_t r78, r4f;
    static uint32_t seen[2], refilled;          /* dual watch's two frequencies aren't logged each time */
    static uint8_t tokens = 4;                  /* and a scan only every 300 ms, after a burst of 4 */
    if (millis() - refilled >= 300) {
        refilled = millis();
        if (tokens < 4)
            tokens++;
    }
    if (freq_10hz != seen[0] && freq_10hz != seen[1] && tokens) {
        tokens--;
        evlog(EV_TUNE, (uint8_t)(am_mode | narrow_bw << 4), freq_10hz);
        seen[1] = seen[0];
        seen[0] = freq_10hz;
    }
    freq = freq_10hz;
    am = (uint8_t)am_mode;
    narrow = (uint8_t)narrow_bw;
    muted = 0;
    tuned_at = millis();
    squelch_regs(&r78, &r4f);
    carrier = 0;
    bk4819_rx(freq, narrow || am == MOD_SSB, r78, r4f, am ? BK_TONE_NONE : tone, tone_value, am ? 0 : scramble);
    bk4819_ssb(am == MOD_SSB);
    bk4819_af_gain(noise[narrow || am == MOD_SSB ? 4 : 3]);    /* as the bandwidth above */
    board_rf_path(freq, RF_RX);
    set_open(monitor || level == 0 || am == MOD_SSB);
}

uint32_t radio_freq(void)
{
    return freq;
}

void radio_retune(void)
{
    radio_tune(freq, am, narrow);
}

int radio_narrow(void)
{
    return narrow;
}

void radio_set_squelch(int l)
{
    if (l != level)
        evlog(EV_SQUELCH, (uint8_t)l, freq);
    level = (uint8_t)l;
    radio_tune(freq, am, narrow);
}

int radio_squelch(void)
{
    return level;
}

void radio_set_monitor(int on)
{
    monitor = (uint8_t)on;
    set_open(monitor || level == 0 || (carrier && tone_heard()));
}

int radio_monitor(void)
{
    return monitor;
}

/* the channel's receive tone is there (or it has none): REG_0C bit 10 CTCSS, 14 DCS, 15 inverted DCS */
static int tone_heard_in(uint16_t r0c)
{
    static const uint16_t bit[] = {0, 0x0400, 0x4000, 0x8000, 0x4000};
    return am || tone == BK_TONE_NONE || (r0c & bit[tone]);
}

static int tone_heard(void)
{
    return tone_heard_in(bk4819_read(0x0C));
}

/* RSSI raw, glitch and noise readings in one word, as the event log keeps them (src/evlog.h) */
static uint32_t readings(void)
{
    return (bk4819_read(0x67) & 0x1FFu) | (uint32_t)(bk4819_read(0x63) & 0xFF) << 16 |
           (uint32_t)(bk4819_read(0x65) & 0x7F) << 24;
}

/* A signal the squelch keeps out: the squelch closed, but the noise indicator at or below level 1's
   threshold, well under its no-signal 70-80. Logged with its readings at most every 3 s, so the log
   shows what a weak station looked like and which of noise, RSSI and glitches stopped it. */
static void weak_check(void)
{
    if (!level || am || millis() - tuned_at < 50 || (weak_at && millis() - weak_at < 3000))
        return;
    uint8_t n = bk4819_read(0x65) & 0x7F;
    if (n > weak_noise)
        return;
    weak_at = millis() | 1;
    evlog_w(EV_WEAK, n, freq, readings());
}

int radio_poll(void)
{
    if (muted)
        return 0;
    uint16_t r0c = bk4819_read(0x0C);
    uint8_t was = carrier;
    carrier = (r0c & 0x0002) != 0;
    int want = monitor || level == 0 || am == MOD_SSB ||   /* SSB has no carrier to open on */
               (carrier && tone_heard_in(r0c));
    if (want == open) {
        if (carrier != was)
            evlog(EV_CARRIER, carrier, freq);
        if (!open)
            weak_check();
        return 0;
    }
    evlog_w(want ? EV_OPEN : EV_CLOSE, (uint8_t)(bk4819_rssi_dbm() + 160), freq, readings());
    set_open(want);
    return 1;
}

int radio_carrier(void)
{
    return carrier;
}

int radio_open(void)
{
    return open;
}

int radio_busy(void)
{
    return open || carrier;
}

int radio_rssi_dbm(void)
{
    return bk4819_rssi_dbm();
}

void radio_sleep(void)                          /* stock 0x80085f8 */
{
    bk4819_off();
    bk4819_write(0x37, 0x1D00);
}

void radio_mute(void)
{
    muted = 1;
    set_open(0);
}

/* Stock search: two measurements in a row within 250 Hz, averaged. A result outside the front end
   in use (below / above 300 MHz) is discarded. */
static uint32_t last_hit;
static uint8_t hits;

void radio_search_start(void)
{
    radio_mute();
    bk4819_search(1);
    hits = 0;
}

int radio_search_poll(uint32_t *found)
{
    uint32_t f;
    if (!bk4819_search_result(&f))
        return 0;
    int uhf_path = freq >= 30000000U, uhf_hit = f >= 30000000U;
    if (f < 1800000 || f > 66000000 || uhf_path != uhf_hit) {
        hits = 0;
        return 0;
    }
    if (hits && (f > last_hit ? f - last_hit : last_hit - f) < 25) {
        f = (f + last_hit) / 2;
        *found = (f + 125) / 250 * 250;     /* nearest 2.5 kHz: on the 5, 12.5 and 25 kHz grids */
        hits = 0;
        return 1;
    }
    last_hit = f;
    hits = 1;
    return 0;
}

void radio_search_stop(void)
{
    bk4819_search(0);
    radio_tune(freq, am, narrow);
}
