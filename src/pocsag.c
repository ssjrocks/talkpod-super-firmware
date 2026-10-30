/* POCSAG paging (ITU-R M.584) at 512, 1200 or 2400 bit/s on the BK4819's direct FSK modem
 * (drivers/bk4819.c, BK_FSK_POCSAG).
 *
 * Codeword: 32 bits, most significant first: a flag (0 address, 1 message), 20 data bits, 10
 * BCH(31,21) check bits (generator 0x769) and even parity. A batch is the sync codeword 7CD215D8
 * and 8 frames of 2 codewords. A pager with RIC r listens in frame r & 7; its address codeword
 * carries r >> 3 and two function bits. Alphanumeric text is 7-bit characters, numeric text 4-bit
 * digits, each least significant bit first; messages end at the next address or idle codeword.
 *
 * Receive: the chip syncs on the first sync codeword and captures 255 bytes (63 codewords, so up
 * to almost four batches); decoding, single-bit error correction and the later sync codewords are
 * handled here. Which way round the FSK is depends on the sender, so the receiver alternates its
 * polarity every 100 ms until a batch syncs. Send: numeric (function 0) when the text is all
 * digits, else alphanumeric (function 3); at most 3 batches.
 */
#include <string.h>
#include "pocsag.h"
#include "bk4819.h"
#include "msg.h"
#include "settings.h"
#include "system.h"
#include "tx.h"

#define SYNC       0x7CD215D8U
#define IDLE       0x7A89C197U
#define RX_LEN     254                 /* even: the FIFO is read in words */
#define TX_PRE     50                  /* preamble bytes ahead of the codewords (+ 16 + 4 from the chip) */
#define BATCHES    3
#define RX_TIMEOUT 5000                /* 254 bytes take 4 s at 512 bit/s */
#define FLIP_MS    100
#define HISTORY    8

pager_cfg_t pager_cfg = {1234567, 1, 0};
const uint16_t pager_bauds[3] = {512, 1200, 2400};
static const char numeric_chars[] = "0123456789*U -)(";

static page_t hist[HISTORY];
static uint8_t count, head, listening, receiving, invert;
static uint8_t buf[256];
static uint16_t rx_n;
static uint32_t rx_started, next_flip;

int pocsag_count(void)
{
    return count;
}

const page_t *pocsag_get(int i)
{
    return i < count ? &hist[(head + HISTORY - 1 - i) % HISTORY] : 0;
}

static page_t *add(uint32_t ric, uint8_t flags)
{
    page_t *p = &hist[head];
    char t[10];
    int n = 0;
    head = (uint8_t)((head + 1) % HISTORY);
    if (count < HISTORY)
        count++;
    memset(p, 0, sizeof *p);
    do {
        t[n++] = (char)('0' + ric % 10);
        ric /= 10;
    } while (ric);
    for (int i = 0; i < n; i++)
        p->from[i] = t[n - 1 - i];
    p->flags = flags;
    return p;
}

/* ---- settings ------------------------------------------------------------------------------ */

void pocsag_init(void)
{
    pager_cfg_t c = pager_cfg;
    if (settings_load(SET_PAGER, &c, sizeof c) && c.speed < 3 && c.ric < (1u << 21))
        pager_cfg = c;
}

void pocsag_save(void)
{
    settings_save(SET_PAGER, &pager_cfg, sizeof pager_cfg);
}

void pocsag_apply(void)
{
    bk4819_pocsag_baud(pager_bauds[pager_cfg.speed]);
}

/* ---- codewords ----------------------------------------------------------------------------- */

static uint32_t parity(uint32_t x)
{
    x ^= x >> 16;
    x ^= x >> 8;
    x ^= x >> 4;
    x ^= x >> 2;
    x ^= x >> 1;
    return x & 1;
}

static uint32_t encode(uint32_t info21)
{
    uint32_t v = info21 << 10;
    for (int i = 30; i >= 10; i--)
        if (v & (1U << i))
            v ^= 0x769U << (i - 10);
    uint32_t cw = (info21 << 10 | v) << 1;
    return cw | parity(cw);
}

static int correct(uint32_t *cw)
{
    if (encode(*cw >> 11) == *cw)
        return 1;
    for (int b = 0; b < 32; b++) {
        uint32_t c = *cw ^ 1U << b;
        if (encode(c >> 11) == c) {
            *cw = c;
            return 1;
        }
    }
    return 0;
}

/* ---- sending ------------------------------------------------------------------------------- */

static int numeric_code(char c)
{
    const char *p = strchr(numeric_chars, c);
    return p && c ? (int)(p - numeric_chars) : -1;
}

int pocsag_send(uint32_t ric, const char *text, uint32_t freq, int narrow, int low_power)
{
    uint32_t cws[BATCHES * 16];
    int n = 0, numeric = 1, len = (int)strlen(text);
    for (int i = 0; i < len; i++)
        if (numeric_code(text[i]) < 0 || text[i] == '*')
            numeric = 0;
    for (int i = 0; i < (int)(ric & 7) * 2; i++)
        cws[n++] = IDLE;
    cws[n++] = encode((ric >> 3) << 2 | (numeric ? 0 : 3));
    uint32_t chunk = 0;
    int bits = 0, per = numeric ? 4 : 7;
    for (int i = 0; i < len || bits; i++) {
        int c = i < len ? (numeric ? numeric_code(text[i]) : text[i] & 0x7F)
                        : numeric ? 0xC : 0;               /* pad: spaces / NUL */
        for (int b = 0; b < per; b++) {
            chunk = chunk << 1 | ((uint32_t)c >> b & 1);
            if (++bits == 20) {
                if (n < BATCHES * 16)
                    cws[n++] = encode(1U << 20 | chunk);
                chunk = 0;
                bits = 0;
            }
        }
        if (i >= len && !bits)
            break;
    }
    if (n < BATCHES * 16)
        cws[n++] = IDLE;                                    /* ends the message */
    int batches = (n + 15) / 16, total = TX_PRE + batches * 68;
    memset(buf, 0xAA, TX_PRE);
    for (int b = 0, k = TX_PRE; b < batches; b++) {
        for (int i = -1; i < 16; i++, k += 4) {
            uint32_t cw = i < 0 ? SYNC : b * 16 + i < n ? cws[b * 16 + i] : IDLE;
            buf[k] = (uint8_t)(cw >> 24);
            buf[k + 1] = (uint8_t)(cw >> 16);
            buf[k + 2] = (uint8_t)(cw >> 8);
            buf[k + 3] = (uint8_t)cw;
        }
    }
    if (!tx_allowed(freq))
        return -1;
    if (!tx_start(freq, narrow, low_power, BK_TONE_NONE, 0, 0))
        return 0;
    delay_ms(50);
    bk4819_fsk_tx_invert(pager_cfg.invert);
    int sent = bk4819_fsk_send(buf, total, narrow);
    bk4819_fsk_tx_invert(0);
    tx_stop();
    if (sent) {
        page_t *p = add(ric, (uint8_t)(PAGE_OUT | (numeric ? PAGE_NUMERIC : 0)));
        strncpy(p->text, text, sizeof p->text - 1);
    }
    return sent;
}

/* ---- receiving ----------------------------------------------------------------------------- */

void pocsag_listen(int on)
{
    listening = (uint8_t)on;
    receiving = 0;
    if (on) {
        bk4819_pocsag_baud(pager_bauds[pager_cfg.speed]);
        bk4819_fsk_setup(BK_FSK_POCSAG, RX_LEN);
        next_flip = millis() + FLIP_MS;
    } else {
        invert = 0;
        bk4819_fsk_invert(0);
        msg_set_modem(msg_modem());
    }
}

int pocsag_receiving(void)
{
    return receiving;
}

static page_t *cur;                     /* the page being decoded */
static uint8_t cur_n, cur_numeric;
static char cur_text[48];
static uint8_t cur_len;
static uint32_t acc;
static uint8_t acc_bits;

static void text_bit(int b)
{
    int per = cur_numeric ? 4 : 7;
    acc |= (uint32_t)b << acc_bits;
    if (++acc_bits < per)
        return;
    char c = cur_numeric ? numeric_chars[acc & 15] : (char)acc;
    acc = 0;
    acc_bits = 0;
    if (cur_len < sizeof cur_text - 1 && c >= 32 && c < 127)
        cur_text[cur_len++] = c;
}

static int finish(void)
{
    if (!cur)
        return 0;
    while (cur_len && cur_text[cur_len - 1] == ' ')
        cur_len--;
    cur_text[cur_len] = 0;
    strcpy(cur->text, cur_len ? cur_text : cur_n ? "" : "(tone only)");
    cur = 0;
    return PAGE_EV_NEW;
}

static int decode(void)
{
    int ev = 0;
    for (int i = 0; i + 4 <= rx_n; i += 4) {
        uint32_t cw = (uint32_t)buf[i] << 24 | (uint32_t)buf[i + 1] << 16 | buf[i + 2] << 8 | buf[i + 3];
        int pos = (i / 4) % 17;                             /* 16: the next batch's sync codeword */
        if (!correct(&cw)) {
            ev |= finish();
            break;                                          /* noise: the transmission is over */
        }
        if (pos == 16) {
            if (cw != SYNC)
                break;
            continue;
        }
        if (cw == IDLE) {
            ev |= finish();
        } else if (!(cw & 0x80000000U)) {
            ev |= finish();
            uint32_t ric = (cw >> 13 & 0x3FFFF) << 3 | (uint32_t)(pos / 2);
            int func = cw >> 11 & 3;
            cur_numeric = func == 0;
            cur = add(ric, cur_numeric ? PAGE_NUMERIC : 0);
            cur_n = 0;
            cur_len = 0;
            acc = 0;
            acc_bits = 0;
        } else if (cur) {
            for (int b = 19; b >= 0; b--)
                text_bit((int)(cw >> (11 + b) & 1));
            cur_n++;
        }
    }
    return ev | finish();
}

static void rx_end(void)
{
    receiving = 0;
    bk4819_fsk_rearm();
}

int pocsag_poll(void)
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
        ev |= PAGE_EV_RX;
    }
    if ((f & 0x1000) && receiving && rx_n + 2 <= RX_LEN) {
        uint16_t w = bk4819_fsk_word();
        buf[rx_n++] = (uint8_t)w;
        buf[rx_n++] = (uint8_t)(w >> 8);
    }
    if (f & 0x2000) {
        if (receiving) {
            while (rx_n + 2 <= RX_LEN) {
                uint16_t w = bk4819_fsk_word();
                buf[rx_n++] = (uint8_t)w;
                buf[rx_n++] = (uint8_t)(w >> 8);
            }
            ev |= decode() | PAGE_EV_RX;
        }
        rx_end();
    } else if (receiving && now - rx_started > RX_TIMEOUT) {
        rx_end();
        ev |= PAGE_EV_RX;
    } else if (!receiving && (int32_t)(now - next_flip) >= 0) {
        next_flip = now + FLIP_MS;
        invert ^= 1;
        bk4819_fsk_invert(invert);
    }
    return ev;
}
