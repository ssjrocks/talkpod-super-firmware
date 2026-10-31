/* Transmit, as the stock firmware does it (0x800c000..: key-up, 0x800c14e: key-down), with one
 * change of order: the RF path is switched to transmit before the BK4819 transmits and the PA power
 * comes last, so RF never meets a path that is not ready for it.
 *
 * PA power (stock 0x800961c / 0x8009aa4): REG_36 = bias << 8 | gain, the bias from this radio's
 * calibration (flash 0xF000 high, 0xF080 low; one byte per frequency segment), the gain 0xFF for
 * high and 0xD7 for low. A bias of 0 or 0xFF (not calibrated) gives 0x007F: PA bias off.
 */
#include "tx.h"
#include "bk4819.h"
#include "board.h"
#include "radio.h"
#include "spiflash.h"
#include "stock.h"
#include "system.h"

static uint8_t bias_high[8], bias_low[8];
static uint8_t active;
static uint8_t tail_dcs;
static uint32_t started, tx_freq;
#define TAIL_MS 300                             /* stock counts 0x28 ticks of its timer task */

void tx_init(void)
{
    spiflash_read(0xF000, bias_high, sizeof bias_high);
    spiflash_read(0xF080, bias_low, sizeof bias_low);
    uint8_t cal[3];
    spiflash_read(0xF210, cal, sizeof cal);
    if (cal[0] != 0xFF)
        bk4819_cal(cal[0], cal[1], cal[2]);
}

int tx_allowed(uint32_t f)
{
    if (f < 1800000 || f > 130000000)           /* 18 MHz .. 1.3 GHz */
        return 0;
    if (f > 66000000 && f < 84000000)           /* the BK4819 has no synthesiser range here */
        return 0;
    if (f >= 10800000 && f <= 13800000)         /* aviation band: never */
        return 0;
    if (f >= 40600000 && f <= 40610000)         /* distress beacons: never */
        return 0;
    return 1;
}

/* calibration byte for the frequency, as stock 0x800961c */
static int segment(uint32_t f)
{
    if (f >= 45500000) return 2;
    if (f >= 42000000) return 1;
    if (f >= 40000000) return 0;
    if (f >= 30000000) return 3;
    if (f >= 20000000) return 7;
    if (f >= 16600000) return 6;
    if (f >= 14500000) return 5;
    if (f >= 13000000) return 4;
    return 0;
}

uint16_t tx_power_reg(uint32_t f, int low_power)
{
    uint8_t bias = (low_power ? bias_low : bias_high)[segment(f)];
    if (bias == 0 || bias == 0xFF)
        return 0x007F;
    return (uint16_t)(bias << 8 | (low_power ? 0xD7 : 0xFF));
}

int tx_start(uint32_t f, int narrow, int low_power, int tone, uint16_t tone_value, int scramble)
{
    if (!tx_allowed(f))
        return 0;
    bk4819_pa_off();
    radio_mute();                               /* speaker off, audio muted */
    board_led(0, 0);
    board_rf_path(f, RF_OFF);
    delay_ms(50);                               /* stock waits 50 ms after PA off */
    board_rf_path(f, RF_TX);
    bk4819_ssb(0);                              /* the other slot may have been listening in SSB */
    bk4819_tx(f, narrow, tx_power_reg(f, low_power), tone, tone_value, scramble);
    board_led(1, 0);
    tail_dcs = tone >= BK_TONE_DCS;
    tx_freq = f;
    started = millis();
    active = 1;
    return 1;
}

/* stock 0x800c494: Roger, then the squelch tail; 0x800b274: Roger is 1000 and 850 Hz for 80 ms each,
   or a data packet */
void tx_end(void)
{
    if (!active)
        return;
    if (stock.roger == 1) {
        bk4819_tone_tx(1);
        bk4819_tone_hz(1000);
        delay_ms(80);
        bk4819_tone_hz(850);
        delay_ms(80);
        bk4819_tone_tx(0);
    } else if (stock.roger == 2) {
        bk4819_roger_data();
    }
    if (stock.ste) {
        bk4819_tail(tail_dcs, stock.tailphase & 3);
        delay_ms(TAIL_MS);
    }
    tx_stop();
}

/* DTMF digits from flash, ending at the first value above 15, at stock's speed list (selected at
   0xA007 on, 0xA008 off) */
static void send_digits(uint32_t addr, int n)
{
    static const uint16_t speed[5] = {50, 100, 200, 300, 500};
    uint8_t code[5], t[2];
    spiflash_read(addr, code, (uint32_t)n);
    spiflash_read(0xA007, t, sizeof t);
    if (!active || code[0] > 15)
        return;
    bk4819_tone_tx(1);
    for (int i = 0; i < n && code[i] <= 15; i++) {
        bk4819_dtmf(code[i]);
        delay_ms(speed[t[0] < 5 ? t[0] : 1]);
        bk4819_dtmf(-1);
        delay_ms(speed[t[1] < 5 ? t[1] : 1]);
    }
    bk4819_tone_tx(0);
}

void tx_ptt_id(int group)
{
    send_digits(0xA020 + (uint32_t)group * 16, 5);
}

void tx_ani(void)
{
    send_digits(0xB000 + (uint32_t)stock.ani * 3, 3);
}

void tx_dtmf_key(int digit)
{
    if (!active)
        return;
    bk4819_tone_tx(digit >= 0);
    if (digit >= 0)
        bk4819_dtmf(digit);
}

void tx_tone_burst(int on)
{
    static const uint16_t hz[4] = {1000, 1450, 1750, 2100};
    if (!active)
        return;
    bk4819_tone_tx(on);
    if (on)
        bk4819_tone_hz(hz[stock.tone & 3]);
}

void tx_stop(void)
{
    if (!active)
        return;
    bk4819_pa_off();
    bk4819_off();
    board_led(0, 0);
    board_rf_path(tx_freq, RF_OFF);
    active = 0;
}

int tx_active(void)
{
    return active;
}

uint32_t tx_seconds(void)
{
    return active ? (millis() - started) / 1000 : 0;
}
