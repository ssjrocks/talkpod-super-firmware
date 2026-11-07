/* Morse code (MCW): text sent as a tone keyed on and off on the FM carrier, so any FM radio on the
 * channel hears it. Standard timing: a dit is one unit of 1200 / WPM ms (PARIS), a dah three, one unit
 * between elements, three between letters, seven between words. The carrier comes up LEAD_MS before
 * the first letter, for receivers' squelch and repeaters; the channel's CTCSS / DCS goes out as with
 * voice. The tone is keyed from the main loop against absolute times, so a slow screen update stretches
 * one element at most and the timing doesn't drift. */
#include "morse.h"
#include "bk4819.h"
#include "system.h"
#include "tx.h"

#define LEAD_MS 500
#define TAIL_MS 150

static const uint8_t letters[26] = {
    0x05, 0x18, 0x1A, 0x0C, 0x02, 0x12, 0x0E, 0x10, 0x04, 0x17, 0x0D, 0x14, 0x07, 0x06, 0x0F, 0x16, 0x1D,
    0x0A, 0x08, 0x03, 0x09, 0x11, 0x0B, 0x19, 0x1B, 0x1C,
};
static const uint8_t digits[10] = {0x3F, 0x2F, 0x27, 0x23, 0x21, 0x20, 0x30, 0x38, 0x3C, 0x3E};
static const char punct[] = ".,?'!/()&:;=+-_\"@";
static const uint8_t punct_code[] = {
    0x55, 0x73, 0x4C, 0x5E, 0x6B, 0x32, 0x36, 0x6D, 0x28, 0x78, 0x6A, 0x31, 0x2A, 0x61, 0x4D, 0x52, 0x5A,
};

enum { IDLE, GAP, ON, TAIL };

static const char *text;
static uint8_t state, code, left;       /* code's elements still to send */
static uint8_t sent;                    /* a letter went out: a space now makes a word gap */
static int pos;
static uint32_t unit, next_at;

uint8_t morse_code(char c)
{
    if (c >= 'a' && c <= 'z')
        c = (char)(c - 32);
    if (c >= 'A' && c <= 'Z')
        return letters[c - 'A'];
    if (c >= '0' && c <= '9')
        return digits[c - '0'];
    for (int i = 0; punct[i]; i++)
        if (punct[i] == c)
            return punct_code[i];
    return 0;
}

static int elements(uint8_t c)
{
    int n = 0;
    while (c > 1) {
        c >>= 1;
        n++;
    }
    return n;
}

int morse_start(const char *t, uint32_t freq, int narrow, int low_power, int tone, uint16_t tone_value, int wpm)
{
    int any = 0;
    for (int i = 0; t[i]; i++)
        any |= morse_code(t[i]) != 0;
    if (!any)
        return 0;
    if (!tx_start(freq, narrow, low_power, tone, tone_value, 0))
        return -1;
    bk4819_tone_tx(1);
    bk4819_tone_key(0);
    bk4819_tone_hz(MORSE_HZ);
    text = t;
    pos = -1;
    left = 0;
    sent = 0;
    unit = 1200u / (uint32_t)(wpm > 0 ? wpm : MORSE_WPM);
    next_at = millis() + LEAD_MS;
    state = GAP;
    return 1;
}

void morse_stop(void)
{
    if (state == IDLE)
        return;
    state = IDLE;
    bk4819_tone_tx(0);
    tx_stop();
}

int morse_active(void)
{
    return state != IDLE;
}

int morse_pos(void)
{
    return pos;
}

void morse_poll(void)
{
    if (state == IDLE)
        return;
    if (!tx_active()) {                         /* the time-out, or PTT, ended the transmission */
        state = IDLE;
        return;
    }
    if ((int32_t)(millis() - next_at) < 0)
        return;
    switch (state) {
    case GAP:
        if (!left) {                            /* the next letter; a space adds the rest of a word gap */
            int space = 0;
            while (text[++pos] && !(code = morse_code(text[pos])))
                space |= text[pos] == ' ';
            if (!text[pos]) {
                next_at += TAIL_MS;
                state = TAIL;
                break;
            }
            left = (uint8_t)elements(code);
            if (space && sent) {
                next_at += 4 * unit;            /* 3 units after the last letter, 7 in all */
                break;
            }
        }
        left--;
        sent = 1;
        bk4819_tone_key(1);
        next_at += (code >> left & 1 ? 3 : 1) * unit;
        state = ON;
        break;
    case ON:
        bk4819_tone_key(0);
        next_at += (left ? 1 : 3) * unit;
        state = GAP;
        break;
    case TAIL:
        state = IDLE;
        bk4819_tone_tx(0);
        tx_end();                               /* Roger and the squelch tail, as after voice */
        break;
    }
}
