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
#include "system.h"

static uint8_t bias_high[8], bias_low[8];
static uint8_t active;
static uint32_t started, tx_freq;

void tx_init(void)
{
    spiflash_read(0xF000, bias_high, sizeof bias_high);
    spiflash_read(0xF080, bias_low, sizeof bias_low);
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
    bk4819_tx(f, narrow, tx_power_reg(f, low_power), tone, tone_value, scramble);
    board_led(1, 0);
    tx_freq = f;
    started = millis();
    active = 1;
    return 1;
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
