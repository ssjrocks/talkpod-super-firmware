/* APRS: AX.25 UI frames on AFSK 1200 (Bell 202: 1200 Hz mark, 2200 Hz space, NRZI, HDLC flags and
 * bit stuffing, CRC-16 FCS; bytes go least significant bit first).
 *
 * Sending: the BK4819's tone generator replaces the microphone and its frequency is switched at the
 * bit boundaries, timed with the CPU's cycle counter (the main loop waits meanwhile, about a second).
 *
 * Receiving (experimental): the MCU has no input for received audio (PA0 is a filtered level), and
 * the BK4819 has no Bell 202 demodulator. So its FFSK demodulator runs at 1200 bit/s and, after a
 * sync on four HDLC flags, hands over raw tone decisions (drivers/bk4819.c, BK_FSK_AFSK); NRZI,
 * flags, unstuffing and the FCS are done here, trying both bit orders. Which tone the flags start
 * on is up to the sender, so the receiver alternates its polarity every 150 ms until one syncs.
 *
 * Packets sent: status beacon ">text", messages ":ADDRESSEE:text{N" and acks ":ADDRESSEE:ackN"
 * to APZA36 (APZ = experimental software) over the path set in the menu.
 */
#include <string.h>
#include "aprs.h"
#include "bk4819.h"
#include "msg.h"
#include "radio.h"
#include "settings.h"
#include "system.h"
#include "tx.h"

#define CPU_HZ        48000000U        /* the bootloader's PLL setting, kept by this firmware */
#define BIT_CYCLES    (CPU_HZ / 1200)
#define TXDELAY_FLAGS 45               /* 300 ms of flags before the frame */
#define RX_LEN        255
#define RX_TIMEOUT    2500             /* 255 bytes take 1.7 s */
#define FLIP_MS       150
#define HISTORY       8
#define ACK_DELAY     1500

#define DEMCR      (*(volatile uint32_t *)0xE000EDFCU)
#define DWT_CTRL   (*(volatile uint32_t *)0xE0001000U)
#define DWT_CYCCNT (*(volatile uint32_t *)0xE0001004U)

aprs_cfg_t aprs_cfg = {"", 1, "A36SE CFW"};
const char *const aprs_path_names[4] = {"none", "WIDE1-1", "WIDE2-1", "WIDE1+2"};

static aprs_pkt_t hist[HISTORY];
static uint8_t count, head, unread, listening, receiving, invert;
static uint8_t rx_buf[RX_LEN], frame[RX_LEN];
static uint16_t rx_n;
static uint32_t rx_started, next_flip, ack_due, ack_freq;
static uint8_t ack_narrow;
static char ack_to[10], ack_id[6];
static char seen[4][16];                /* "CALL{id" of the last messages to us: no duplicates */
static uint8_t seen_next;
static uint16_t next_id = 1;

/* ---- history ------------------------------------------------------------------------------- */

int aprs_count(void)
{
    return count;
}

const aprs_pkt_t *aprs_get(int i)
{
    return i < count ? &hist[(head + HISTORY - 1 - i) % HISTORY] : 0;
}

int aprs_unread(void)
{
    return unread;
}

void aprs_read_all(void)
{
    unread = 0;
}

static aprs_pkt_t *add(const char *from, uint8_t flags)
{
    aprs_pkt_t *p = &hist[head];
    head = (uint8_t)((head + 1) % HISTORY);
    if (count < HISTORY)
        count++;
    memset(p, 0, sizeof *p);
    strncpy(p->from, from, sizeof p->from - 1);
    p->flags = flags;
    return p;
}

static void copy_text(char *d, const char *s, int n, int max)
{
    int k = 0;
    for (int i = 0; i < n && k < max - 1; i++)
        d[k++] = (char)(s[i] >= 32 && s[i] < 127 ? s[i] : '.');
    d[k] = 0;
}

/* ---- settings ------------------------------------------------------------------------------ */

void aprs_init(void)
{
    aprs_cfg_t c = aprs_cfg;
    if (settings_load(SET_APRS, &c, sizeof c)) {
        c.call[sizeof c.call - 1] = 0;
        c.text[sizeof c.text - 1] = 0;
        if (c.path > 3)
            c.path = 1;
        aprs_cfg = c;
    }
}

void aprs_save(void)
{
    settings_save(SET_APRS, &aprs_cfg, sizeof aprs_cfg);
}

/* ---- AX.25 frames -------------------------------------------------------------------------- */

static uint16_t fcs(const uint8_t *p, int n)
{
    uint16_t c = 0xFFFF;
    while (n--) {
        c ^= *p++;
        for (int i = 0; i < 8; i++)
            c = (uint16_t)(c & 1 ? (c >> 1) ^ 0x8408 : c >> 1);
    }
    return (uint16_t)~c;
}

/* "VK2ABC-9" -> 6 shifted characters and the SSID byte; 0 if it is not a callsign */
static int put_addr(uint8_t *d, const char *call, uint8_t ssid_bits)
{
    int i = 0, ssid = 0;
    while (i < 6 && call[i] && call[i] != '-') {
        d[i] = (uint8_t)(call[i] << 1);
        i++;
    }
    if (!i)
        return 0;
    for (int k = i; k < 6; k++)
        d[k] = ' ' << 1;
    const char *s = strchr(call, '-');
    if (s)
        while (*++s >= '0' && *s <= '9')
            ssid = ssid * 10 + (*s - '0');
    d[6] = (uint8_t)(ssid_bits | (ssid & 15) << 1);
    return 1;
}

static int build(const char *info, int info_len)
{
    static const char *const digis[4][2] = {{0, 0}, {"WIDE1-1", 0}, {"WIDE2-1", 0},
                                            {"WIDE1-1", "WIDE2-1"}};
    const char *const *path = digis[aprs_cfg.path & 3];
    int n = 0, ndigi = (path[0] != 0) + (path[1] != 0);
    put_addr(frame, "APZA36", 0xE0);                         /* destination, command bit */
    if (!put_addr(frame + 7, aprs_cfg.call, ndigi ? 0x60 : 0x61))
        return 0;
    n = 14;
    for (int i = 0; i < ndigi; i++, n += 7)
        put_addr(frame + n, path[i], (uint8_t)(i == ndigi - 1 ? 0x61 : 0x60));
    frame[n++] = 0x03;                                       /* UI frame */
    frame[n++] = 0xF0;                                       /* no layer 3 */
    memcpy(frame + n, info, (size_t)info_len);
    n += info_len;
    uint16_t f = fcs(frame, n);
    frame[n++] = (uint8_t)f;
    frame[n++] = (uint8_t)(f >> 8);
    return n;
}

/* ---- sending ------------------------------------------------------------------------------- */

static uint32_t edge;
static uint16_t tone;
static uint8_t ones;

static void send_bit(int b)
{
    while ((int32_t)(DWT_CYCCNT - edge) < 0) {}
    if (!b) {                                               /* NRZI: a 0 changes the tone */
        tone = tone == 1200 ? 2200 : 1200;
        bk4819_tone_hz(tone);
    }
    edge += BIT_CYCLES;
    fwdgt_feed();
}

static void send_byte(uint8_t v, int stuff)
{
    for (int i = 0; i < 8; i++, v >>= 1) {
        send_bit(v & 1);
        if (!stuff)
            continue;
        if (!(v & 1)) {
            ones = 0;
        } else if (++ones == 5) {
            send_bit(0);
            ones = 0;
        }
    }
}

static int transmit(int n, uint32_t freq, int narrow, int low_power)
{
    if (!n)
        return -2;
    if (!tx_allowed(freq))
        return -1;
    if (!tx_start(freq, narrow, low_power, BK_TONE_NONE, 0, 0))
        return 0;
    tone = 1200;
    bk4819_tone_hz(tone);
    bk4819_tone_tx(1);
    DEMCR |= 1U << 24;
    DWT_CTRL |= 1;
    edge = DWT_CYCCNT + BIT_CYCLES;
    for (int i = 0; i < TXDELAY_FLAGS; i++)
        send_byte(0x7E, 0);
    ones = 0;
    for (int i = 0; i < n; i++)
        send_byte(frame[i], 1);
    for (int i = 0; i < 3; i++)
        send_byte(0x7E, 0);
    while ((int32_t)(DWT_CYCCNT - edge) < 0) {}             /* the last bit's time */
    bk4819_tone_tx(0);
    tx_stop();
    return 1;
}

int aprs_beacon(uint32_t freq, int narrow, int low_power)
{
    char info[20] = ">";
    strcat(info, aprs_cfg.text);
    int r = transmit(build(info, (int)strlen(info)), freq, narrow, low_power);
    if (r > 0)
        copy_text(add("beacon", APRS_OUT)->text, info, (int)strlen(info), 48);
    return r;
}

/* ":ADDRESSEE:" with the addressee padded to nine characters */
static int addressee(char *d, const char *to)
{
    int n = 0;
    d[n++] = ':';
    for (int i = 0; i < 9; i++)
        d[n++] = *to ? *to++ : ' ';
    d[n++] = ':';
    return n;
}

static char *put_num(char *d, uint16_t v)
{
    char t[6];
    int n = 0;
    do {
        t[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n)
        *d++ = t[--n];
    *d = 0;
    return d;
}

int aprs_message(const char *to, const char *text, uint32_t freq, int narrow, int low_power)
{
    char info[64], id[6];
    int n = addressee(info, to);
    size_t tl = strlen(text);
    memcpy(info + n, text, tl);
    n += (int)tl;
    info[n++] = '{';
    put_num(id, next_id);
    strcpy(info + n, id);
    n += (int)strlen(id);
    int r = transmit(build(info, n), freq, narrow, low_power);
    if (r > 0) {
        aprs_pkt_t *p = add(to, APRS_OUT | APRS_MSG);
        copy_text(p->text, text, (int)tl, sizeof p->text);
        strcpy(p->id, id);
        next_id = (uint16_t)(next_id % 999 + 1);
    }
    return r;
}

int aprs_send_ack(int low_power)
{
    char info[24];
    int n = addressee(info, ack_to);
    strcpy(info + n, "ack");
    strcpy(info + n + 3, ack_id);
    return transmit(build(info, (int)strlen(info)), ack_freq, ack_narrow, low_power);
}

/* ---- receiving ----------------------------------------------------------------------------- */

void aprs_listen(int on)
{
    listening = (uint8_t)on;
    receiving = 0;
    if (on) {
        bk4819_fsk_setup(BK_FSK_AFSK, RX_LEN);
        next_flip = millis() + FLIP_MS;
    } else {
        invert = 0;
        bk4819_fsk_invert(0);
        msg_set_modem(msg_modem());                         /* back to UV-K5 messages */
    }
}

int aprs_receiving(void)
{
    return receiving;
}

static int bit_at(int i, int order)
{
    uint8_t b = rx_buf[(i >> 3) ^ (order >> 1)];            /* order 2, 3: bytes of a word swapped */
    return order & 1 ? (b >> (i & 7)) & 1 : (b >> (7 - (i & 7))) & 1;
}

/* NRZI and HDLC on the captured tone decisions: a frame with a good FCS into frame[]; its length */
static int deframe(int order)
{
    int nbits = (rx_n & ~1) * 8, len = -1, nb = 0, ones_run = 0;
    uint8_t sr = 0, cur = 0;
    int prev = bit_at(0, order);
    for (int i = 1; i < nbits; i++) {
        int t = bit_at(i, order), d = t == prev;            /* no tone change: a 1 */
        prev = t;
        sr = (uint8_t)(sr >> 1 | d << 7);
        if (sr == 0x7E) {                                   /* flag */
            if (len >= 18 && fcs(frame, len - 2) == (frame[len - 2] | frame[len - 1] << 8))
                return len - 2;
            len = 0;
            nb = 0;
            ones_run = 0;
            continue;
        }
        if (len < 0)
            continue;
        if (d) {
            if (++ones_run >= 7) {                          /* abort */
                len = -1;
                continue;
            }
        } else {
            if (ones_run == 5) {                            /* stuffed 0 */
                ones_run = 0;
                continue;
            }
            ones_run = 0;
        }
        cur = (uint8_t)(cur >> 1 | d << 7);
        if (++nb == 8) {
            nb = 0;
            if (len < RX_LEN)
                frame[len++] = cur;
            else
                len = -1;
        }
    }
    return 0;
}

static void call_at(const uint8_t *a, char *out)
{
    int n = 0;
    for (int i = 0; i < 6 && (a[i] >> 1) != ' '; i++)
        out[n++] = (char)(a[i] >> 1);
    int ssid = (a[6] >> 1) & 15;
    if (ssid) {
        out[n++] = '-';
        if (ssid >= 10)
            out[n++] = '1';
        out[n++] = (char)('0' + ssid % 10);
    }
    out[n] = 0;
}

static int same(const char *a, const char *b)            /* newlib's strcmp is 730 bytes */
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}

static int same_call(const char *a, const char *b, int n)  /* b padded with spaces to n */
{
    int i = 0;
    for (; i < n && a[i]; i++)
        if (a[i] != b[i])
            return 0;
    return i == n || b[i] == ' ';
}

static int message(const char *from, const char *info, int n)
{
    char text[48];
    if (n < 11 || info[10] != ':')
        return 0;
    const char *body = info + 11;
    int bl = n - 11;
    if (!aprs_cfg.call[0] || !same_call(aprs_cfg.call, info + 1, 9)) {
        char to[10];
        int k = 0;
        while (k < 9 && info[1 + k] != ' ') {
            to[k] = info[1 + k];
            k++;
        }
        to[k] = 0;
        aprs_pkt_t *p = add(from, APRS_MSG);
        p->text[0] = '>';
        strcpy(p->text + 1, to);
        size_t l = strlen(p->text);
        p->text[l++] = ' ';
        copy_text(p->text + l, body, bl, (int)(sizeof p->text - l));
        return APRS_EV_NEW;
    }
    if (bl >= 3 && (!memcmp(body, "ack", 3) || !memcmp(body, "rej", 3))) {
        copy_text(text, body + 3, bl - 3, 6);
        for (int i = 0; i < count; i++) {
            aprs_pkt_t *p = (aprs_pkt_t *)aprs_get(i);
            if ((p->flags & APRS_OUT) && same(p->id, text) && same(p->from, from)) {
                p->flags |= APRS_ACKED;
                return APRS_EV_ACKED;
            }
        }
        return 0;
    }
    const char *brace = memchr(body, '{', (size_t)bl);
    int tl = brace ? (int)(brace - body) : bl;
    int ev = 0;
    if (brace) {                                             /* numbered: answer with an ack */
        char key[16];
        copy_text(ack_id, brace + 1, bl - tl - 1, sizeof ack_id);
        char *e = strchr(ack_id, '}');                      /* "{12}ab" reply-ack form */
        if (e)
            *e = 0;
        strcpy(ack_to, from);
        ack_freq = radio_freq();
        ack_narrow = (uint8_t)radio_narrow();
        ack_due = (millis() + ACK_DELAY) | 1;
        strcpy(key, from);
        strcat(key, "{");
        strcat(key, ack_id);
        for (int i = 0; i < 4; i++)
            if (same(seen[i], key))
                return 0;                                   /* a repeat: ack it, show it once */
        strcpy(seen[seen_next], key);
        seen_next = (uint8_t)((seen_next + 1) % 4);
    }
    aprs_pkt_t *p = add(from, APRS_MSG | APRS_TO_ME);
    copy_text(p->text, body, tl, sizeof p->text);
    if (unread < 99)
        unread++;
    ev |= APRS_EV_NEW;
    return ev;
}

static int received(int len)
{
    char from[10];
    int a = 14;
    while (a + 7 <= len && !(frame[a - 1] & 1))             /* digipeaters */
        a += 7;
    if (a + 2 > len || frame[a] != 0x03 || frame[a + 1] != 0xF0)
        return 0;
    call_at(frame + 7, from);
    const char *info = (const char *)frame + a + 2;
    int n = len - a - 2;
    if (n <= 0)
        return 0;
    if (info[0] == ':')
        return message(from, info, n);
    aprs_pkt_t *p = add(from, 0);
    switch (info[0]) {
    case '!': case '=': case '/': case '@': {
        int off = info[0] == '/' || info[0] == '@' ? 8 : 1;   /* after a timestamp */
        if (n >= off + 19 && info[off] >= '0' && info[off] <= '9') {
            char t[48];                                     /* "3351.20S 15112.40E comment" */
            copy_text(t, info + off, 8, 9);
            t[8] = ' ';
            copy_text(t + 9, info + off + 9, 9, 10);
            t[18] = ' ';
            copy_text(t + 19, info + off + 19, n - off - 19, 48 - 19);
            strcpy(p->text, t);
        } else {
            strcpy(p->text, "position ");
            if (n > off + 13)
                copy_text(p->text + 9, info + off + 13, n - off - 13, 48 - 9);
        }
        break;
    }
    case '`': case '\'':
        strcpy(p->text, "Mic-E position");
        break;
    case ';':
        strcpy(p->text, "object ");
        copy_text(p->text + 7, info + 1, n > 10 ? 9 : n - 1, 10);
        break;
    default:
        copy_text(p->text, info[0] == '>' ? info + 1 : info, info[0] == '>' ? n - 1 : n,
                  sizeof p->text);
        break;
    }
    return APRS_EV_NEW;
}

static void rx_end(void)
{
    receiving = 0;
    bk4819_fsk_rearm();
}

int aprs_poll(void)
{
    int ev = 0;
    uint32_t now = millis();
    if (!listening)
        return 0;
    uint16_t f = bk4819_irq();
    if (f & 0x0002) {
        receiving = 1;
        rx_n = 0;
        rx_started = now;
        ev |= APRS_EV_RX;
    }
    if ((f & 0x1000) && receiving && rx_n + 2 <= RX_LEN) {
        uint16_t w = bk4819_fsk_word();
        rx_buf[rx_n++] = (uint8_t)w;
        rx_buf[rx_n++] = (uint8_t)(w >> 8);
    }
    if (f & 0x2000) {
        if (receiving) {
            while (rx_n + 2 <= RX_LEN) {
                uint16_t w = bk4819_fsk_word();
                rx_buf[rx_n++] = (uint8_t)w;
                rx_buf[rx_n++] = (uint8_t)(w >> 8);
            }
            for (int order = 0; order < 4; order++) {
                int len = deframe(order);
                if (len) {
                    ev |= received(len);
                    break;
                }
            }
            ev |= APRS_EV_RX;
        }
        rx_end();
    } else if (receiving && now - rx_started > RX_TIMEOUT) {
        rx_end();
        ev |= APRS_EV_RX;
    } else if (!receiving && (int32_t)(now - next_flip) >= 0) {
        next_flip = now + FLIP_MS;
        invert ^= 1;
        bk4819_fsk_invert(invert);
    }
    if (ack_due && !receiving && (int32_t)(now - ack_due) >= 0) {
        ack_due = 0;
        ev |= APRS_EV_ACK_DUE;
    }
    return ev;
}
