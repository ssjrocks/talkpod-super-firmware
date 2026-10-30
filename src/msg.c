/* Text messages, in the format of the UV-K5 messenger (kamilsss655's UV-K5 firmware, 2024,
 * app/messenger.c), so this radio can chat with UV-K5s that run it. The modem settings are in
 * drivers/bk4819.c (bk4819_fsk_*).
 *
 * Packet, 44 bytes after 16 preamble bytes and the sync word 30 72 57 6C: a type byte (100 message,
 * 101 encrypted message, 102 receipt), 30 bytes of text padded with zeros (29 characters at most, so
 * it ends in a zero), and a 13-byte nonce that only encrypted messages use (zeros otherwise). A
 * radio that gets a message answers 700 ms later with a receipt (type 102, text five 0xFF bytes);
 * the sender then marks its newest message delivered. Encrypted messages (ChaCha20 with a key made
 * from a password) are listed as "(encrypted)".
 */
#include <string.h>
#include "msg.h"
#include "bk4819.h"
#include "radio.h"
#include "system.h"
#include "tx.h"

#define PKT_LEN      44
#define RX_LEN       46     /* as the UV-K5 receives: the length rounded up to even, plus 2 */
#define T_MESSAGE    100
#define T_SECRET     101
#define T_RECEIPT    102
#define HISTORY      10
#define RECEIPT_MS   700
#define RX_TIMEOUT   3000   /* a whole packet takes 0.43 s at 1200 bit/s, 1.14 s at 450 */

static msg_t hist[HISTORY];
static uint8_t count, head;                     /* head: where the next message goes */
static uint8_t modem = BK_FSK_1200, receipts = 1, unread;
static uint8_t rx_buf[RX_LEN], rx_n, receiving;
static uint32_t rx_started, receipt_due, receipt_freq;
static uint8_t receipt_narrow;

void msg_init(int m, int r)
{
    modem = (uint8_t)m;
    receipts = (uint8_t)r;
    bk4819_fsk_setup(modem, RX_LEN);
}

void msg_set_modem(int m)
{
    modem = (uint8_t)m;
    receiving = 0;
    bk4819_fsk_setup(modem, RX_LEN);
}

int msg_modem(void)
{
    return modem;
}

void msg_set_receipts(int on)
{
    receipts = (uint8_t)on;
}

int msg_receipts(void)
{
    return receipts;
}

int msg_receiving(void)
{
    return receiving;
}

int msg_count(void)
{
    return count;
}

const msg_t *msg_get(int i)
{
    return i < count ? &hist[(head + HISTORY - 1 - i) % HISTORY] : 0;
}

int msg_unread(void)
{
    return unread;
}

void msg_read_all(void)
{
    unread = 0;
}

static msg_t *add(uint8_t flags)
{
    msg_t *m = &hist[head];
    head = (uint8_t)((head + 1) % HISTORY);
    if (count < HISTORY)
        count++;
    memset(m, 0, sizeof *m);
    m->flags = flags;
    return m;
}

static void store(uint16_t w)
{
    if (rx_n < RX_LEN)
        rx_buf[rx_n++] = (uint8_t)w;
    if (rx_n < RX_LEN)
        rx_buf[rx_n++] = (uint8_t)(w >> 8);
}

static int received(void)
{
    uint8_t type = rx_buf[0];
    if (type == T_RECEIPT) {
        for (int i = 0; i < count; i++) {
            msg_t *m = (msg_t *)msg_get(i);
            if (m->flags & MSG_OUT) {
                m->flags |= MSG_DELIVERED;
                return MSG_EV_DELIVERED;
            }
        }
        return 0;
    }
    if (type != T_MESSAGE && type != T_SECRET)
        return 0;                               /* noise that happened to match the sync word */
    char text[31];
    int n = 0;
    if (type == T_SECRET) {
        strcpy(text, "(encrypted)");
        n = 11;
    } else {
        while (n < 30 && rx_buf[1 + n]) {
            uint8_t c = rx_buf[1 + n];
            text[n++] = (char)(c >= 32 && c < 127 ? c : '?');
        }
        text[n] = 0;
        if (!n)
            return 0;
    }
    msg_t *m = add((uint8_t)(MSG_IN | (type == T_SECRET ? MSG_SECRET : 0)));
    memcpy(m->text, text, (size_t)n + 1);
    if (unread < 99)
        unread++;
    if (receipts) {
        receipt_due = (millis() + RECEIPT_MS) | 1;
        receipt_freq = radio_freq();
        receipt_narrow = (uint8_t)radio_narrow();
    }
    return MSG_EV_NEW;
}

static void rx_end(void)
{
    receiving = 0;
    radio_audio_hold(HOLD_DATA, 0);
    bk4819_fsk_rearm();
}

int msg_poll(void)
{
    int ev = 0;
    if (modem == BK_FSK_OFF)
        return 0;
    uint16_t f = bk4819_irq();
    if (f & 0x0002) {                           /* sync word: a packet starts */
        receiving = 1;
        rx_n = 0;
        rx_started = millis();
        radio_audio_hold(HOLD_DATA, 1);         /* nobody wants to hear the modem */
        ev |= MSG_EV_RX;
    }
    if ((f & 0x1000) && receiving)
        store(bk4819_fsk_word());
    if (f & 0x2000) {                           /* the packet is complete */
        if (receiving) {
            while (rx_n < PKT_LEN)              /* words the 10 ms polling has not fetched yet */
                store(bk4819_fsk_word());
            ev |= received() | MSG_EV_RX;
        }
        rx_end();
    } else if (receiving && millis() - rx_started > RX_TIMEOUT) {
        rx_end();
        ev |= MSG_EV_RX;
    }
    if (receipt_due && !receiving && (int32_t)(millis() - receipt_due) >= 0) {
        receipt_due = 0;
        ev |= MSG_EV_RECEIPT_DUE;
    }
    return ev;
}

static int transmit(const uint8_t *pkt, uint32_t freq, int narrow, int low_power)
{
    if (!tx_allowed(freq))
        return -1;
    if (!tx_start(freq, narrow, low_power))
        return 0;
    delay_ms(50);
    int sent = bk4819_fsk_send(pkt, PKT_LEN, narrow);
    tx_stop();
    return sent;
}

int msg_send(const char *text, uint32_t freq, int narrow, int low_power)
{
    uint8_t pkt[PKT_LEN] = {T_MESSAGE};
    size_t n = strlen(text);
    if (n > MSG_TEXT)
        n = MSG_TEXT;
    memcpy(pkt + 1, text, n);
    int r = transmit(pkt, freq, narrow, low_power);
    if (r > 0) {
        msg_t *m = add(MSG_OUT);
        memcpy(m->text, text, n);
    }
    return r;
}

int msg_send_receipt(int low_power)
{
    uint8_t pkt[PKT_LEN] = {T_RECEIPT, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    return transmit(pkt, receipt_freq, receipt_narrow, low_power);
}
