#include "scan.h"
#include "channels.h"
#include "radio.h"
#include "system.h"

#define SETTLE_MS  35           /* after a retune, before reading the squelch */
#define HANG_MS    2000         /* resume this long after the signal went away */

static uint8_t mode, holding, am, narrow;
static int8_t dir;
static uint32_t lo, hi, step, next_at, closed_since;
static int channel;

int channel_is_am(uint32_t f)
{
    return f >= 10800000 && f < 13700000;
}

static void retune(void)
{
    if (mode == SCAN_VFO) {
        uint32_t f = radio_freq();
        f = dir > 0 ? f + step : f - step;
        if (f > hi)
            f = lo;
        if (f < lo)
            f = hi;
        radio_tune(f, am, narrow);
    } else {
        channel_t ch;
        int n = channel_next(channel, dir, 1);
        if (!n || !channel_read(n, &ch)) {
            mode = SCAN_OFF;
            return;
        }
        channel = n;
        radio_tune(ch.rx, channel_is_am(ch.rx), ch.narrow);
    }
    next_at = millis() + SETTLE_MS;
}

void scan_start_vfo(uint32_t from_lo, uint32_t to_hi, uint32_t st, int d, int am_mode, int nb)
{
    lo = from_lo;
    hi = to_hi;
    step = st;
    dir = (int8_t)d;
    am = (uint8_t)am_mode;
    narrow = (uint8_t)nb;
    mode = SCAN_VFO;
    holding = 0;
    radio_set_monitor(0);
    retune();
}

void scan_start_ch(int from, int d)
{
    channel = from;
    dir = (int8_t)d;
    mode = SCAN_CH;
    holding = 0;
    radio_set_monitor(0);
    retune();
}

void scan_stop(void)
{
    mode = SCAN_OFF;
    holding = 0;
}

int scan_mode(void)
{
    return mode;
}

int scan_holding(void)
{
    return holding;
}

int scan_channel(void)
{
    return channel;
}

int scan_poll(void)
{
    if (mode == SCAN_OFF || (int32_t)(millis() - next_at) < 0)
        return SCAN_EV_NONE;
    radio_poll();
    if (!holding) {
        if (radio_open()) {
            holding = 1;
            closed_since = 0;
            return SCAN_EV_HOLD;
        }
        retune();
        return mode == SCAN_OFF ? SCAN_EV_RESUME : SCAN_EV_STEP;
    }
    if (radio_open()) {
        closed_since = 0;
    } else if (!closed_since) {
        closed_since = millis() | 1;
    } else if (millis() - closed_since >= HANG_MS) {
        holding = 0;
        retune();
        return SCAN_EV_RESUME;
    }
    return SCAN_EV_NONE;
}
