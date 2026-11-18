/* Tunes: short melodies for fun (a few notes to about 10 s) on the BK4819's tone generator, through the
 * speaker, or on the transmitter as an audio tone on the FM carrier, as Morse is sent (src/morse.c): the
 * channel's CTCSS / DCS go with it, Roger and Tail cut don't, and the carrier comes up LEAD_MS first for
 * receivers' squelch and repeaters.
 *
 * A tune is notes of two bytes: the MIDI note number (60 middle C, 0 a rest; bit 7 G: glide to the next
 * note's pitch over this one, without a gap), then the length in 10 ms. A note that doesn't glide ends
 * with GAP_MS of silence, so repeated notes are heard as two. A tune may have a second voice (a bass) on
 * the chip's second tone generator, in the same form and keeping its own time; tone 2's register is the
 * modem's bit rate too, so the modem is set up again afterwards. The tunes are public domain, made up
 * here (Ta-da!, Level up, Sad trombone, Boing), or the user's own (Keyboard jam, from a MIDI recording
 * by tools/midi2tune.py: channels 16 and 7, the bass two octaves up, 1.25 x the tempo). */
#include "tune.h"
#include "bk4819.h"
#include "board.h"
#include "system.h"
#include "tx.h"

#define LEAD_MS 400
#define TAIL_MS 150
#define GAP_MS  20
#define G       0x80

static const uint8_t t_jam[] = {
    84, 20, 81, 40, 84, 20, 83, 40, 84, 20, 83, 40, 79, 120, 81, 20, 88, 20, 86, 40,
    84, 20, 83, 40, 84, 20, 83, 40, 79, 140, 84, 20, 81, 40, 84, 20, 83, 40, 84, 20,
    83, 40, 79, 120, 81, 20, 81, 20, 77, 40, 81, 20, 79, 40, 81, 20, 79, 40, 72, 140,
    84, 20, 81, 40, 84, 20, 83, 40,
};
static const uint8_t t_jam_bass[] = {
    65, 7, 0, 13, 65, 7, 0, 13, 65, 7, 0, 13, 65, 20, 64, 7, 0, 13, 64, 7,
    0, 13, 64, 7, 0, 13, 64, 7, 0, 13, 64, 7, 0, 13, 60, 20, 62, 20, 64, 20,
    65, 7, 0, 13, 65, 7, 0, 13, 65, 7, 0, 13, 65, 7, 0, 13, 65, 7, 0, 13,
    65, 7, 0, 13, 65, 7, 0, 13, 65, 20, 64, 7, 0, 13, 64, 7, 0, 13, 64, 7,
    0, 13, 64, 7, 0, 13, 64, 7, 0, 13, 60, 20, 62, 20, 64, 20, 65, 7, 0, 13,
    65, 7, 0, 13, 65, 7, 0, 13, 65, 7, 0, 13, 65, 7, 0, 13, 65, 7, 0, 13,
    65, 7, 0, 13, 65, 20, 64, 7, 0, 13, 64, 7, 0, 13, 64, 7, 0, 13, 64, 7,
    0, 13, 64, 7, 0, 13, 64, 7, 0, 13, 64, 7, 0, 13, 64, 20, 62, 7, 0, 13,
    62, 7, 0, 13, 62, 7, 0, 13, 62, 7, 0, 13, 62, 7, 0, 13, 62, 7, 0, 13,
    62, 7, 0, 13, 62, 20, 60, 7, 0, 13, 60, 7, 0, 13, 60, 7, 0, 13, 60, 7,
    0, 13, 60, 7, 0, 13, 60, 20, 62, 20, 64, 20, 65, 7, 0, 13, 65, 7, 0, 13,
    65, 7, 0, 13, 65, 7, 0, 13, 65, 7, 0, 13, 65, 7, 0, 13, 65, 7, 0, 13,
    65, 20, 64, 7, 0, 13, 64, 7,
};
static const uint8_t t_tada[] = {72, 8, 76, 8, 79, 8, 0, 4, 84, 60};
static const uint8_t t_levelup[] = {72, 6, 76, 6, 79, 6, 84, 6, 88, 6, 91, 6, 96, 30};
static const uint8_t t_charge[] = {67, 15, 72, 15, 76, 15, 79, 30, 76, 15, 79, 60};
static const uint8_t t_shave[] = {72, 40, 67, 20, 67, 20, 69, 40, 67, 40, 0, 40, 71, 40, 72, 40};
static const uint8_t t_trombone[] = {
    70 | G, 40, 69, 10, 69 | G, 40, 68, 10, 68 | G, 40, 67, 10,
    67 | G, 8, 66 | G, 8, 67 | G, 8, 66 | G, 8, 67 | G, 8, 66 | G, 8, 67 | G, 8, 66 | G, 8, 67 | G, 8, 66, 30,
};
static const uint8_t t_boing[] = {60 | G, 12, 84 | G, 25, 60, 5};
static const uint8_t t_ode[] = {
    76, 35, 76, 35, 77, 35, 79, 35, 79, 35, 77, 35, 76, 35, 74, 35,
    72, 35, 72, 35, 74, 35, 76, 35, 76, 52, 74, 18, 74, 70,
};
static const uint8_t t_elise[] = {
    76, 15, 75, 15, 76, 15, 75, 15, 76, 15, 71, 15, 74, 15, 72, 15, 69, 30, 0, 15,
    60, 15, 64, 15, 69, 15, 71, 30, 0, 15, 64, 15, 68, 15, 71, 15, 72, 30, 0, 15,
    64, 15, 76, 15, 75, 15, 76, 15, 75, 15, 76, 15, 71, 15, 74, 15, 72, 15, 69, 45,
};
static const uint8_t t_birthday[] = {
    67, 30, 67, 10, 69, 40, 67, 40, 72, 40, 71, 80,
    67, 30, 67, 10, 69, 40, 67, 40, 74, 40, 72, 80,
    67, 30, 67, 10, 79, 40, 76, 40, 72, 40, 71, 40, 69, 80,
    77, 30, 77, 10, 76, 40, 72, 40, 74, 40, 72, 80,
};
static const uint8_t t_twinkle[] = {
    72, 30, 72, 30, 79, 30, 79, 30, 81, 30, 81, 30, 79, 60,
    77, 30, 77, 30, 76, 30, 76, 30, 74, 30, 74, 30, 72, 60,
};

typedef struct {
    const char *name;
    const uint8_t *notes, *bass;        /* bass: the second voice, or 0 */
    uint8_t n, nb;                      /* notes, two bytes each */
} tune_t;
#define T(name, a) {name, a, 0, sizeof a / 2, 0}
#define T2(name, a, b) {name, a, b, sizeof a / 2, sizeof b / 2}
static const tune_t tunes[] = {
    T2("Keyboard jam", t_jam, t_jam_bass), T("Ta-da!", t_tada), T("Level up", t_levelup), T("Charge!", t_charge), T("Shave & haircut", t_shave),
    T("Sad trombone", t_trombone), T("Boing", t_boing), T("Ode to joy", t_ode), T("Fur Elise", t_elise),
    T("Happy birthday", t_birthday), T("Twinkle twinkle", t_twinkle),
};
#define TUNES ((int)(sizeof tunes / sizeof tunes[0]))

/* MIDI 108-119 (C8 to B8) in Hz x 4; a lower octave is a shift of these */
static const uint16_t top4[12] = {16744, 17740, 18795, 19912, 21096, 22351, 23680, 25088, 26580, 28160, 29834, 31609};

static uint16_t note_hz(int n)
{
    int shift = 11 - n / 12;            /* x 4, and an octave down per 12 below MIDI 108 */
    return (uint16_t)((top4[n % 12] + (1u << (shift - 1))) >> shift);
}

enum { IDLE, LEAD, PLAY, TAIL };                /* the tune */
enum { V_DONE, V_SOUND, V_SILENT };             /* a voice */

typedef struct {
    const uint8_t *notes;
    uint8_t n, idx, state, glide, on;
    uint16_t hz, from, to, rest;
    uint32_t next_at, glide_at, glide_len;
} voice_t;

static voice_t vs[2];                           /* the melody on tone 1, a bass on tone 2 */
static uint8_t state, on_air, gain;
static uint32_t next_at;                        /* the end of the lead or the tail */

int tune_count(void)
{
    return TUNES;
}

const char *tune_name(int i)
{
    return i >= 0 && i < TUNES ? tunes[i].name : "";
}

static uint32_t voice_ms(const uint8_t *notes, int n)
{
    uint32_t ms = 0;
    for (int k = 0; k < n; k++)
        ms += notes[2 * k + 1] * 10u;
    return ms;
}

uint32_t tune_ms(int i, int air)
{
    if (i < 0 || i >= TUNES)
        return 0;
    uint32_t a = voice_ms(tunes[i].notes, tunes[i].n), b = voice_ms(tunes[i].bass, tunes[i].nb);
    return (a > b ? a : b) + (air ? LEAD_MS + TAIL_MS : 0);
}

int tune_active(void)
{
    return state != IDLE;
}

static void key(voice_t *v, int on)
{
    if (v->on != on) {
        v->on = (uint8_t)on;
        bk4819_tones(vs[0].on, vs[1].on, gain);
    }
}

static void set_hz(int k, uint16_t hz)
{
    if (hz != vs[k].hz) {
        if (k)
            bk4819_tone2_hz(hz);
        else
            bk4819_tone_hz(hz);
        vs[k].hz = hz;
    }
}

static void next_note(int k)
{
    voice_t *v = &vs[k];
    if (v->idx >= v->n) {
        key(v, 0);
        v->state = V_DONE;
        return;
    }
    int p = v->notes[2 * v->idx], n = p & 0x7F;
    uint32_t ms = v->notes[2 * v->idx + 1] * 10u;
    v->idx++;
    v->glide = 0;
    if (!n) {
        key(v, 0);
        v->state = V_SILENT;
        v->next_at += ms;
        return;
    }
    set_hz(k, note_hz(n));
    key(v, 1);
    v->state = V_SOUND;
    if ((p & G) && v->idx < v->n && (v->notes[2 * v->idx] & 0x7F)) {
        v->glide = 1;
        v->from = v->hz;
        v->to = note_hz(v->notes[2 * v->idx] & 0x7F);
        v->glide_at = v->next_at;
        v->glide_len = ms;
        v->next_at += ms;
    } else {
        uint32_t gap = ms > 2 * GAP_MS ? GAP_MS : ms / 2;
        v->rest = (uint16_t)gap;
        v->next_at += ms - gap;
    }
}

static void voice_poll(int k, uint32_t now)
{
    voice_t *v = &vs[k];
    if (v->state == V_DONE)
        return;
    if (v->glide && v->state == V_SOUND && now - v->glide_at < v->glide_len)
        set_hz(k, (uint16_t)(v->from + ((int32_t)v->to - v->from) * (int32_t)(now - v->glide_at) / (int32_t)v->glide_len));
    if ((int32_t)(now - v->next_at) < 0)
        return;
    if (v->state == V_SOUND && !v->glide) {
        key(v, 0);
        v->state = V_SILENT;
        v->next_at += v->rest;
    } else {
        next_note(k);                           /* after a rest or a gap; a glide goes straight on */
    }
}

static void end_local(void)
{
    bk4819_beep(0, 0);
    board_speaker(SPK_TUNE, 0);
    bk4819_fsk_restore();
    state = IDLE;
}

int tune_start(int i, uint32_t freq, int narrow, int low_power, int tone, uint16_t tone_value)
{
    if (i < 0 || i >= TUNES)
        return 0;
    const tune_t *t = &tunes[i];
    on_air = freq != 0;
    if (on_air) {
        if (!tx_start(freq, narrow, low_power, tone, tone_value, 0))
            return -1;
        bk4819_tone_tx(1);
        next_at = millis() + LEAD_MS;
    } else {
        board_speaker(SPK_TUNE, 1);
        bk4819_beep(1, 440);                    /* the tone generator to the speaker */
        bk4819_write(0x48, 0xB3FF);             /* as loud as the shell's test tone */
        next_at = millis() + 100;               /* the screen is drawn meanwhile */
    }
    gain = t->bass ? 40 : 66;                   /* two tones together deviate as much as one at 66 */
    for (int k = 0; k < 2; k++) {
        voice_t *v = &vs[k];
        v->notes = k ? t->bass : t->notes;
        v->n = k ? t->nb : t->n;
        v->idx = v->glide = v->on = 0;
        v->hz = 0;
        v->state = v->notes ? V_SILENT : V_DONE;
    }
    bk4819_tones(0, 0, gain);
    state = LEAD;
    return 1;
}

void tune_stop(void)
{
    if (state == IDLE)
        return;
    if (on_air) {
        state = IDLE;
        bk4819_tone_tx(0);
        tx_stop();
        bk4819_fsk_restore();
    } else {
        end_local();
    }
}

void tune_poll(void)
{
    if (state == IDLE)
        return;
    if (on_air && !tx_active()) {               /* the time-out, or PTT, ended the transmission */
        state = IDLE;
        return;
    }
    uint32_t now = millis();
    if (state == PLAY) {
        voice_poll(0, now);
        voice_poll(1, now);
        if (vs[0].state == V_DONE && vs[1].state == V_DONE) {
            if (on_air) {                       /* the carrier a little longer */
                state = TAIL;
                next_at = now + TAIL_MS;
            } else {
                end_local();
            }
        }
        return;
    }
    if ((int32_t)(now - next_at) < 0)
        return;
    if (state == LEAD) {
        state = PLAY;
        vs[0].next_at = vs[1].next_at = next_at;
    } else {                                    /* TAIL */
        state = IDLE;
        bk4819_tone_tx(0);
        tx_stop();                              /* no Roger, no squelch tail: those are for voice */
        bk4819_fsk_restore();
    }
}
