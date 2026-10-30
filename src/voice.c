/* Voice prompts and UI sounds from the sound pack in the external flash (tools/soundpack.py), played
 * the way the stock firmware plays its prompts: PWM on PA11 (TMR1 channel 4, alternate function 2)
 * into the speaker amplifier.
 *
 * Audio is IMA ADPCM, high nibble first, 16-bit predictor. TMR1 counts at 48 MHz with a period of
 * 978 clocks (49 kHz carrier) and interrupts every 6 periods (repetition counter 5): 8180 samples
 * per second, decoded in the interrupt. A 2 KB ring buffer, refilled from flash by voice_poll() in
 * the main loop, covers 0.5 s of stalls. PA11 idles high (as the bootloader leaves it), so playback
 * ramps from 100 % duty to the middle and back to avoid a click.
 */
#include "voice.h"
#include "board.h"
#include "radio.h"
#include "spiflash.h"

#define TMR1_CTRL1 (*(volatile uint32_t *)0x40012C00U)
#define TMR1_IDEN  (*(volatile uint32_t *)0x40012C0CU)
#define TMR1_ISTS  (*(volatile uint32_t *)0x40012C10U)
#define TMR1_SWEVT (*(volatile uint32_t *)0x40012C14U)
#define TMR1_CM2   (*(volatile uint32_t *)0x40012C1CU)
#define TMR1_CCTRL (*(volatile uint32_t *)0x40012C20U)
#define TMR1_DIV   (*(volatile uint32_t *)0x40012C28U)
#define TMR1_PR    (*(volatile uint32_t *)0x40012C2CU)
#define TMR1_RPR   (*(volatile uint32_t *)0x40012C30U)
#define TMR1_C4DT  (*(volatile uint32_t *)0x40012C40U)
#define TMR1_BRK   (*(volatile uint32_t *)0x40012C44U)

#define PERIOD  978
#define MID     (PERIOD / 2)
#define RAMP    160                     /* samples: 20 ms */
#define BUF     2048

/* pack: "SNDP", u16 count, u16 sample rate, then count x {u16 id, u16 0, u32 offset, u32 length} */
#define MAGIC   0x50444E53U

enum { IDLE, RAMP_IN, PLAY, RAMP_OUT, DONE };

static const uint16_t steps[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66,
    73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408,
    449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630,
    9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
};
static const int8_t index_adj[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

static volatile uint8_t buf[BUF];
static volatile uint16_t rd, wr;        /* ring: the interrupt reads at rd, voice_poll writes at wr */
static volatile uint8_t state, half, byte;
static volatile uint32_t left;          /* bytes not yet decoded */
static volatile int32_t pred;
static volatile uint8_t idx;
static volatile uint16_t ramp, last;
static volatile uint32_t isr_count;
static uint32_t src, src_left;          /* flash not yet buffered */
static uint8_t timer_ready;

void TIMER0_BRK_UP_TRG_COM_IRQHandler(void)
{
    uint16_t out = last;
    TMR1_ISTS = 0;
    isr_count++;
    switch (state) {
    case RAMP_IN:
        out = (uint16_t)(PERIOD - (PERIOD - MID) * ramp / RAMP);
        if (++ramp >= RAMP)
            state = PLAY;
        break;
    case PLAY: {
        int code;
        if (!half) {
            if (!left) {
                state = RAMP_OUT;
                ramp = 0;
                break;
            }
            if (rd == wr)
                break;                  /* buffer ran dry: hold the level */
            byte = buf[rd];
            rd = (uint16_t)((rd + 1) % BUF);
            left--;
            code = byte >> 4;
        } else {
            code = byte & 15;
        }
        half ^= 1;
        int step = steps[idx];
        int i = idx + index_adj[code & 7];
        idx = (uint8_t)(i < 0 ? 0 : i > 88 ? 88 : i);
        int diff = step >> 3;
        if (code & 4) diff += step;
        if (code & 2) diff += step >> 1;
        if (code & 1) diff += step >> 2;
        pred = code & 8 ? pred - diff : pred + diff;
        if (pred > 32767) pred = 32767;
        if (pred < -32768) pred = -32768;
        out = (uint16_t)(MID + (pred * 61) / 4096);  /* +-32768 -> +-488 */
        break;
    }
    case RAMP_OUT:
        out = (uint16_t)(last + (PERIOD - last) * ramp / RAMP);
        if (++ramp >= RAMP)
            state = DONE;
        break;
    default:
        out = PERIOD;                   /* faded out: stay at the idle level */
        break;
    }
    if (state != RAMP_OUT)
        last = out;
    TMR1_C4DT = out;
}

static void timer_setup(void)
{
    rcu_periph_clock_enable(RCU_TIMER0);
    TMR1_CTRL1 = 0;
    TMR1_DIV = 0;
    TMR1_PR = PERIOD - 1;
    TMR1_RPR = 5;                       /* an update every 6 periods */
    TMR1_CM2 = 0x6800;                  /* channel 4: PWM mode 1, preload */
    TMR1_CCTRL = 0x1000;                /* channel 4 output on */
    TMR1_C4DT = PERIOD;
    TMR1_SWEVT = 1;                     /* load the settings */
    TMR1_ISTS = 0;
    TMR1_IDEN = 1;                      /* update interrupt */
    NVIC_EnableIRQ(TIMER0_BRK_UP_TRG_COM_IRQn);
    timer_ready = 1;
}

static void stop(void)
{
    TMR1_CTRL1 = 0;
    TMR1_BRK = 0;
    gpio_bit_set(GPIOA, AUX_PA11_PIN);
    gpio_mode_set(GPIOA, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, AUX_PA11_PIN);
    state = IDLE;
    board_speaker(SPK_VOICE, 0);
    radio_audio_hold(0);
}

static void refill(void)
{
    uint8_t chunk[64];
    while (src_left) {
        uint16_t free = (uint16_t)((rd + BUF - wr - 1) % BUF);
        uint32_t n = src_left < sizeof chunk ? src_left : sizeof chunk;
        if (free < n)
            return;
        spiflash_read(src, chunk, n);
        for (uint32_t i = 0; i < n; i++)
            buf[(wr + i) % BUF] = chunk[i];
        wr = (uint16_t)((wr + n) % BUF);
        src += n;
        src_left -= n;
    }
}

int voice_play(int id)
{
    uint8_t h[12];
    uint32_t offset = 0, length = 0;
    spiflash_read(FLASH_SOUNDS, h, 8);
    uint16_t count = (uint16_t)(h[4] | h[5] << 8);
    if ((h[0] | h[1] << 8 | h[2] << 16 | (uint32_t)h[3] << 24) != MAGIC || count > 64)
        return 0;
    for (int i = 0; i < count && !length; i++) {
        spiflash_read(FLASH_SOUNDS + 8 + i * 12, h, 12);
        if ((h[0] | h[1] << 8) == id) {
            offset = h[4] | h[5] << 8 | h[6] << 16 | (uint32_t)h[7] << 24;
            length = h[8] | h[9] << 8 | h[10] << 16 | (uint32_t)h[11] << 24;
        }
    }
    if (!length || offset + length > FLASH_SPLASH - FLASH_SOUNDS)
        return 0;

    if (!timer_ready)
        timer_setup();
    TMR1_CTRL1 = 0;
    state = IDLE;
    rd = wr = 0;
    src = FLASH_SOUNDS + offset;
    src_left = length;
    left = length;
    half = 0;
    pred = 0;
    idx = 0;
    ramp = 0;
    refill();

    radio_audio_hold(1);
    board_speaker(SPK_VOICE, 1);
    TMR1_C4DT = PERIOD;
    gpio_af_set(GPIOA, GPIO_AF_2, AUX_PA11_PIN);
    gpio_mode_set(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, AUX_PA11_PIN);
    gpio_output_options_set(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, AUX_PA11_PIN);
    state = RAMP_IN;
    TMR1_BRK = 0x8000;                  /* main output on */
    TMR1_CTRL1 = 1;
    return 1;
}

void voice_debug(uint32_t v[8])
{
    v[0] = state;
    v[1] = isr_count;
    v[2] = rd;
    v[3] = wr;
    v[4] = left;
    v[5] = TMR1_C4DT;
    v[6] = TMR1_CTRL1 | TMR1_BRK << 8;
    v[7] = *(volatile uint32_t *)0x40012C24U;   /* counter */
}

void voice_stop(void)
{
    if (state != IDLE)
        stop();
}

int voice_busy(void)
{
    return state != IDLE;
}

void voice_poll(void)
{
    if (state == IDLE)
        return;
    if (state == DONE)
        stop();
    else
        refill();
}
