#include "scan.h"
#include "bk4819.h"
#include "channels.h"
#include "radio.h"
#include "system.h"

/* The BK4819 raises its squelch bit 45-65 ms after a retune onto a signal (measured with the stock
   open delay of 5 in REG_4E: about 40 ms + 5 ms per delay step), so each step gets up to DWELL_MS.
   The noise indicator (REG_65) settles within 30 ms: 10-32 on a signal, 53-69 without. Where it is
   clearly above the chip's own close threshold the squelch cannot open, and the scan moves on at
   SETTLE_MS. */
#define SETTLE_MS     30
#define DWELL_MS      80
#define NOISE_MARGIN  6
#define HANG_MS       2000      /* resume this long after the signal went away */

static uint8_t mode, holding, am, narrow;
static int8_t dir;
static uint32_t lo, hi, step, tuned_at, closed_since;
static int channel;

/* frequencies locked out of the scan (static, interference) until cleared or power-off */
#define LOCKOUT_MAX 32
static uint32_t lockout[LOCKOUT_MAX];
static uint8_t nlock;

static int locked(uint32_t f)
{
    for (int i = 0; i < nlock; i++)
        if (lockout[i] == f)
            return 1;
    return 0;
}

int channel_is_am(uint32_t f)
{
    return f >= 10800000 && f < 13700000;
}

static void retune(void)
{
    if (mode == SCAN_VFO) {
        uint32_t f = radio_freq();
        for (int i = 0; i <= LOCKOUT_MAX; i++) {         /* past locked-out frequencies */
            f = dir > 0 ? f + step : f - step;
            if (f > hi)
                f = lo;
            if (f < lo)
                f = hi;
            if (!locked(f))
                break;
        }
        radio_tune(f, am, narrow);
    } else {
        channel_t ch;
        int n = channel;
        for (int i = 0; i < CHANNEL_COUNT; i++) {
            n = channel_next(n, dir, 1);
            if (!n || !channel_read(n, &ch)) {
                mode = SCAN_OFF;
                return;
            }
            if (!locked(ch.rx))
                break;
        }
        channel = n;
        radio_tune(ch.rx, channel_is_am(ch.rx), ch.narrow);
    }
    tuned_at = millis();
}

void scan_skip(int d)
{
    dir = (int8_t)d;
    holding = 0;
    retune();
}

void scan_lockout(void)
{
    uint32_t f = radio_freq();
    if (!locked(f) && nlock < LOCKOUT_MAX)
        lockout[nlock++] = f;
    holding = 0;
    retune();
}

void scan_lockout_clear(void)
{
    nlock = 0;
}

int scan_lockouts(void)
{
    return nlock;
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
    radio_tune(radio_freq() / step * step, am, narrow);    /* onto the step grid */
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
    uint32_t since = millis() - tuned_at;
    if (mode == SCAN_OFF || since < SETTLE_MS)
        return SCAN_EV_NONE;
    radio_poll();
    if (!holding) {
        if (radio_open()) {
            holding = 1;
            closed_since = 0;
            return SCAN_EV_HOLD;
        }
        if (since < DWELL_MS && (bk4819_read(0x65) & 0x7F) < (bk4819_read(0x4F) >> 8) + NOISE_MARGIN)
            return SCAN_EV_NONE;
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
