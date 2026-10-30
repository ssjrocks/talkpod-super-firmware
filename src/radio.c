/* Receive core. Squelch thresholds are computed exactly as stock 0x80098f4 does, from the per-radio
 * calibration tables in the external flash (docs/stock-firmware.md), and the BK4819 makes the
 * open/close decision (REG_0C bit 1). */
#include "radio.h"
#include "bk4819.h"
#include "board.h"
#include "spiflash.h"

static uint8_t sq_open[10], sq_close[10];   /* RSSI per squelch level (0xF0C0, 0xF0D0) */
static uint8_t corr_400[16], corr_130[16], corr_200[16], corr_300[16];  /* per-frequency offsets */
static uint8_t noise[16];                   /* 0xF210: [5 + band] threshold, [9] hysteresis */

static uint32_t freq = 47690000;
static uint8_t am, narrow, level = 1, monitor, open, muted, hold;

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

/* Stock thresholds, widened. Measured on this radio (470-490 MHz, no signal): the noise indicator
   reads 70-80 in narrow mode but 50-79 in wide, where a third of all frequencies sit at the stock
   threshold (open below 55) and the squelch opens on static. A signal reads 10-32. The RSSI floor
   (about -95 dBm) is above every stock level (-120..-108 dBm at 476 MHz), so stock's levels barely
   differ here. So: wide mode lowers the noise threshold by WIDE_NOISE, and each level above 1
   tightens noise by LEVEL_NOISE and raises the RSSI threshold by LEVEL_RSSI (raw units: 2 per dB).
   Narrow level 1 stays exactly stock. */
#define WIDE_NOISE   15
#define LEVEL_NOISE  3
#define LEVEL_RSSI   6

static void squelch_regs(uint16_t *reg78, uint16_t *reg4f)
{
    int band;
    uint8_t c = correction(freq, &band);
    int o = sq_open[level], cl = sq_close[level];
    if (o > c) {
        o -= c;
        cl = cl > c ? cl - c : 0;
    }
    if (level > 1) {
        o += (level - 1) * LEVEL_RSSI;
        cl += (level - 1) * LEVEL_RSSI;
    }
    *reg78 = (uint16_t)((o > 255 ? 255 : o) << 8 | (cl > 255 ? 255 : cl));

    int n = noise[5 + band] & 0x7F;
    if (!narrow)
        n -= WIDE_NOISE;
    if (level > 1)
        n -= (level - 1) * LEVEL_NOISE;
    if (n < 8 + noise[9])
        n = 8 + noise[9];
    *reg4f = (uint16_t)(n << 8 | (n - noise[9]));
}

static void set_open(int on)
{
    open = (uint8_t)on;
    bk4819_af(on && !hold ? (am ? BK_AF_AM : BK_AF_FM) : BK_AF_MUTE);
    board_speaker(SPK_RADIO, on);
    board_led(0, on);
}

void radio_audio_hold(int who, int on)
{
    hold = (uint8_t)(on ? hold | who : hold & ~who);
    set_open(open);
}

void radio_tune(uint32_t freq_10hz, int am_mode, int narrow_bw)
{
    uint16_t r78, r4f;
    freq = freq_10hz;
    am = (uint8_t)am_mode;
    narrow = (uint8_t)narrow_bw;
    muted = 0;
    squelch_regs(&r78, &r4f);
    bk4819_rx(freq, narrow, r78, r4f);
    board_rf_path(freq, RF_RX);
    set_open(monitor || level == 0);
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
    set_open(monitor || level == 0 || bk4819_squelch_open());
}

int radio_monitor(void)
{
    return monitor;
}

int radio_poll(void)
{
    if (muted)
        return 0;
    int want = monitor || level == 0 || bk4819_squelch_open();
    if (want == open)
        return 0;
    set_open(want);
    return 1;
}

int radio_open(void)
{
    return open;
}

int radio_rssi_dbm(void)
{
    return bk4819_rssi_dbm();
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
