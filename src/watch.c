#include "watch.h"
#include "bk4819.h"
#include "channels.h"
#include "radio.h"
#include "scan.h"
#include "system.h"

/* Same timing as the scanner (scan.c): the squelch decides 45-65 ms after a retune, the noise
   indicator within 30 ms. A slot whose noise is clearly too high is left at SETTLE_MS. */
#define SETTLE_MS     30
#define DWELL_MS      80
#define NOISE_MARGIN  6
#define HANG_MS       1000
#define TONE_WAIT_MS  500           /* a carrier without the slot's tone: move on after this */

static slot_t slots[2];
static uint8_t selected, dual = 1, rx, holding, paused;
static uint32_t tuned_at, closed_since, held_at;

static void resolve(slot_t *s)
{
    channel_t ch;
    if (s->is_channel && channel_read(s->channel, &ch)) {
        uint32_t rx_f = s->reverse && ch.tx ? ch.tx : ch.rx, tx_f = s->reverse && ch.tx ? ch.rx : ch.tx;
        if (s->freq != rx_f)
            s->low_power = ch.low_power;        /* a new channel brings its own power */
        s->freq = rx_f;
        s->tx = tx_f;
        s->rx_tone = ch.rx_tone;
        s->tx_tone = ch.tx_tone;
        s->bcl = ch.bcl;
        s->scramble = ch.scramble;
        s->pttid = ch.pttid;
        s->scode = ch.scode;
        s->am = (uint8_t)channel_is_am(ch.rx);
        s->narrow = ch.narrow;
        for (int i = 0; i < 13; i++)
            s->name[i] = ch.name[i];
    } else {
        s->is_channel = 0;
        s->freq = s->tx = s->vfo;
        s->rx_tone.type = s->tx_tone.type = TONE_NONE;
        s->bcl = s->scramble = s->pttid = 0;
        s->am = s->vfo_am;
        s->narrow = s->vfo_narrow;
        s->name[0] = 0;
    }
}

static void tune(int i)
{
    rx = (uint8_t)i;
    radio_set_tone(slots[i].rx_tone.type, slots[i].rx_tone.value, slots[i].scramble);
    radio_tune(slots[i].freq, slots[i].am, slots[i].narrow);
    tuned_at = millis();
}

void watch_init(void)
{
    static const int prefer[2] = {11, 40};          /* UHF CB calling and highway channels */
    channel_t ch;
    int fallback = 0;
    for (int i = 0; i < 2; i++) {
        slot_t *s = &slots[i];
        s->vfo = 47690000;
        s->is_channel = 1;
        if (channel_read(prefer[i], &ch)) {
            s->channel = (uint16_t)prefer[i];
        } else {
            fallback = channel_next(fallback, 1, 0);
            s->channel = (uint16_t)fallback;
            if (!fallback)
                s->is_channel = 0;
        }
        resolve(s);
    }
    tune(selected);
}

slot_t *watch_slot(int i)
{
    return &slots[i];
}

int watch_selected(void)
{
    return selected;
}

void watch_select(int i)
{
    selected = (uint8_t)i;
    holding = 0;
    tune(i);
}

void watch_set_dual(int on)
{
    dual = (uint8_t)on;
    holding = 0;
    tune(selected);
}

int watch_dual(void)
{
    return dual;
}

void watch_update(int i)
{
    resolve(&slots[i]);
    holding = 0;
    tune(i);
}

int watch_rx_slot(void)
{
    return rx;
}

void watch_pause(int on)
{
    paused = (uint8_t)on;
    if (!on) {
        holding = 0;
        tune(selected);
    }
}

int watch_poll(void)
{
    if (paused)
        return WATCH_EV_NONE;
    if (!dual) {
        if (rx != selected)
            tune(selected);
        return radio_poll() ? (radio_open() ? WATCH_EV_OPEN : WATCH_EV_CLOSE) : WATCH_EV_NONE;
    }
    uint32_t since = millis() - tuned_at;
    if (since < SETTLE_MS)
        return WATCH_EV_NONE;
    int changed = radio_poll();
    if (holding) {
        if (!radio_open() && millis() - held_at >= TONE_WAIT_MS && slots[rx].rx_tone.type != TONE_NONE &&
            !radio_monitor()) {
            holding = 0;                    /* someone else's traffic: their tone isn't ours */
            tune(rx ^ 1);
            return WATCH_EV_NONE;
        }
        if (radio_busy()) {
            closed_since = 0;
        } else if (!closed_since) {
            closed_since = millis() | 1;
        } else if (millis() - closed_since >= HANG_MS) {
            holding = 0;
            tune(rx ^ 1);
        }
        return changed ? (radio_open() ? WATCH_EV_OPEN : WATCH_EV_CLOSE) : WATCH_EV_NONE;
    }
    if (radio_busy()) {
        holding = 1;
        held_at = millis();
        closed_since = 0;
        return radio_open() ? WATCH_EV_OPEN : WATCH_EV_NONE;
    }
    if (radio_monitor() || radio_squelch() == 0)
        return WATCH_EV_NONE;               /* always open: no alternating */
    if (since < DWELL_MS && (bk4819_read(0x65) & 0x7F) < (bk4819_read(0x4F) >> 8) + NOISE_MARGIN)
        return WATCH_EV_NONE;
    tune(rx ^ 1);
    return WATCH_EV_NONE;
}
