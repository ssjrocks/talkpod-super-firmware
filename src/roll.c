/* Rolling scrambler (a channel's scramble set to "Rolling", stored as SCRAMBLE_ROLL_STORED).
 *
 * The BK4819's voice inversion, as the fixed types 1-10, but its inversion frequency glides between
 * 2600 and 3500 Hz along a path both radios work out from the shared key (menu "Scramble key") and
 * a 32-bit start value. A listener without the key hears the pitch of the inverted voice wander,
 * which is much harder to follow than one fixed inversion. Only radios with this firmware and the
 * key can follow it; it is privacy against casual listening, not security.
 *
 * At key-up the sender sends the start value in an 8-byte burst on the text-message modem, with its
 * own sync word (UV-K5 messenger radios never sync on it), then glides from the moment the burst
 * ended. A receiver on a rolling channel listens for that sync word instead of messages. When the
 * burst has arrived, it glides along from the burst's end. The path changes at most SLOPE Hz per ms,
 * so a timing error of a few ms between the two radios only shifts the descrambled pitch by tens of
 * Hz, where a hopping scrambler would garble a slice of every hop.
 *
 * Burst: 01, start value (4 bytes, little-endian), key check (top byte of the key's hash), check
 * (2 bytes, little-endian: the low half of FNV-1a over bytes 0-5). tools/roll.py is the reference.
 */
#include <string.h>
#include "roll.h"
#include "bk4819.h"
#include "msg.h"
#include "radio.h"
#include "settings.h"
#include "system.h"
#include "tx.h"

#define LO          2600            /* Hz: the UV-K5's inversion range */
#define HI          3500
#define SLOPE       3               /* Hz per ms at most */
#define RX_LOST_MS  400             /* no carrier for this long: the transmission is over */
#define FNV_START   2166136261u
#define DWT_CTRL    (*(volatile uint32_t *)0xE0001000U)
#define DWT_CYCCNT  (*(volatile uint32_t *)0xE0001004U)

enum { OFF, TX, RX };

roll_cfg_t roll_cfg;

static struct {
    uint32_t rng, t0, dur;          /* the segment from t0 lasts dur ms */
    uint16_t fa, fb;                /* gliding from fa to fb */
} path;
static uint8_t mode;
static uint16_t hz_now;
static uint32_t start, rx_freq, heard, last_ms, salt;

static uint32_t fnv(const uint8_t *p, uint32_t n, uint32_t h)
{
    while (n--) {
        h ^= *p++;
        h *= 16777619u;
    }
    return h;
}

static uint32_t key_hash(void)
{
    return fnv((const uint8_t *)roll_cfg.key, (uint32_t)strlen(roll_cfg.key), FNV_START);
}

static uint32_t rnd(void)                       /* xorshift32 */
{
    uint32_t x = path.rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return path.rng = x;
}

static void segment(void)
{
    uint32_t r = rnd();
    path.t0 += path.dur;
    path.fa = path.fb;
    path.fb = (uint16_t)(LO + r % (HI - LO + 1));
    int d = path.fb - path.fa;
    path.dur = (uint32_t)(d < 0 ? -d : d) / SLOPE + 40 + (r >> 20) % 80;
}

static void path_start(uint32_t iv)
{
    path.rng = (key_hash() ^ iv * 2654435761u) | 1;   /* xorshift needs a state other than 0 */
    path.t0 = path.dur = 0;
    path.fb = (uint16_t)(LO + rnd() % (HI - LO + 1));
    segment();
}

static uint16_t path_hz(uint32_t t)             /* t: ms since the burst ended, never going back */
{
    while (t >= path.t0 + path.dur)
        segment();
    return (uint16_t)(path.fa + ((int32_t)path.fb - path.fa) * (int32_t)(t - path.t0) / (int32_t)path.dur);
}

void roll_init(void)
{
    roll_cfg_t c;
    memset(&c, 0, sizeof c);
    if (settings_load(SET_SCRAMBLE, &c, sizeof c) && memchr(c.key, 0, sizeof c.key))
        roll_cfg = c;
}

void roll_save(void)
{
    settings_save(SET_SCRAMBLE, &roll_cfg, sizeof roll_cfg);
}

int roll_tx(int narrow)
{
    uint8_t p[BK_ROLL_LEN];
    DWT_CTRL |= 1;                              /* the cycle counter: when PTT was pressed */
    salt = salt * 2654435761u ^ DWT_CYCCNT ^ millis();
    uint32_t iv = salt, h = key_hash();
    p[0] = 1;
    for (int i = 0; i < 4; i++)
        p[1 + i] = (uint8_t)(iv >> (8 * i));
    p[5] = (uint8_t)(h >> 24);
    uint32_t c = fnv(p, 6, FNV_START);
    p[6] = (uint8_t)c;
    p[7] = (uint8_t)(c >> 8);
    roll_stop();
    if (!bk4819_fsk_send_roll(p, BK_ROLL_LEN, narrow, msg_modem()))
        return 0;
    start = bk4819_fsk_sent_ms();
    bk4819_fsk_voice();
    path_start(iv);
    mode = TX;
    roll_poll();
    return 1;
}

int roll_rx(const uint8_t *b, uint32_t end_ms)
{
    if (b[0] != 1 || (uint16_t)fnv(b, 6, FNV_START) != (b[6] | b[7] << 8))
        return 0;
    if (!roll_cfg.key[0] || b[5] != (uint8_t)(key_hash() >> 24))
        return -1;
    path_start((uint32_t)(b[1] | b[2] << 8 | b[3] << 16 | (uint32_t)b[4] << 24));
    start = end_ms;
    rx_freq = radio_freq();
    heard = millis();
    mode = RX;
    roll_poll();
    return 1;
}

void roll_poll(void)
{
    if (mode == OFF)
        return;
    uint32_t now = millis();
    if (now == last_ms)
        return;
    last_ms = now;
    if (mode == RX && radio_busy())
        heard = now;
    if (mode == TX ? !tx_active()
                   : tx_active() || radio_freq() != rx_freq || now - heard > RX_LOST_MS) {
        roll_stop();
        return;
    }
    if ((int32_t)(now - start) < 0)
        return;
    uint16_t hz = path_hz(now - start);
    if (hz != hz_now) {
        hz_now = hz;
        bk4819_scramble_hz(hz);
    }
}

/* Turns the scrambler off only when receiving ends on the same frequency: after a retune or a
   transmission start, bk4819_rx() / bk4819_tx() have just set the new channel's scrambler. */
void roll_stop(void)
{
    if (mode == OFF)
        return;
    if (mode == RX && !tx_active() && radio_freq() == rx_freq)
        bk4819_scramble_hz(0);
    mode = OFF;
    hz_now = 0;
}

int roll_active(void)
{
    return mode != OFF;
}
