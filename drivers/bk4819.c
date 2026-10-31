/* BK4819 transceiver: bit-banged 3-wire SPI and the register sequences of the stock firmware
 * (docs/stock-firmware.md), after Jamie793/A36Plus_Radio Drivers/BK4819 (MIT).
 *
 * Frame: CS low, 8 bits register (bit 7 set = read), 16 bits data, CS high; MSB first, the chip
 * samples SDA on the SCK rising edge and we read it just after that edge. SDA (PA3) is shared with
 * the BK1080's I2C data line, so the BK1080 clock (PF6) is held low to keep it from seeing a START.
 */
#include "bk4819.h"
#include "board.h"
#include "system.h"

/* Interrupt enables: stock's 0x07FE (squelch, VOX, tones, FSK sync) plus FSK FIFO almost full (12),
   RX finished (13), FIFO almost empty (14) and TX finished (15) for the data modes. Nothing is
   wired to an MCU pin: the flags are polled in REG_0C bit 0 / REG_02. */
#define IRQ_ENABLE 0xF7FE

static void half_bit(void)
{
    for (volatile int i = 0; i < 12; i++) {}     /* ~2 us at 48 MHz: about 250 kHz */
}

static void sda_output(int on)
{
    gpio_mode_set(BK4819_PORT, on ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT, GPIO_PUPD_NONE, BK4819_SDA_PIN);
}

static void send_bits(uint32_t v, int n)
{
    while (n--) {
        if (v & (1U << n))
            gpio_bit_set(BK4819_PORT, BK4819_SDA_PIN);
        else
            gpio_bit_reset(BK4819_PORT, BK4819_SDA_PIN);
        half_bit();
        gpio_bit_set(BK4819_PORT, BK4819_SCK_PIN);
        half_bit();
        gpio_bit_reset(BK4819_PORT, BK4819_SCK_PIN);
    }
}

void bk4819_write(uint8_t reg, uint16_t val)
{
    gpio_bit_reset(BK4819_CS_PORT, BK4819_CS_PIN);
    half_bit();
    send_bits(((uint32_t)(reg & 0x7F) << 16) | val, 24);
    half_bit();
    gpio_bit_set(BK4819_CS_PORT, BK4819_CS_PIN);
    half_bit();
}

uint16_t bk4819_read(uint8_t reg)
{
    uint16_t v = 0;
    gpio_bit_reset(BK4819_CS_PORT, BK4819_CS_PIN);
    half_bit();
    send_bits(reg | 0x80, 8);
    sda_output(0);
    for (int i = 0; i < 16; i++) {
        half_bit();
        gpio_bit_set(BK4819_PORT, BK4819_SCK_PIN);
        half_bit();
        v = (uint16_t)((v << 1) | (gpio_input_bit_get(BK4819_PORT, BK4819_SDA_PIN) == SET));
        gpio_bit_reset(BK4819_PORT, BK4819_SCK_PIN);
    }
    sda_output(1);
    half_bit();
    gpio_bit_set(BK4819_CS_PORT, BK4819_CS_PIN);
    half_bit();
    return v;
}

void bk4819_init(void)
{
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_GPIOC);
    rcu_periph_clock_enable(RCU_GPIOF);
    gpio_bit_reset(BK1080_SCL_PORT, BK1080_SCL_PIN);
    gpio_mode_set(BK1080_SCL_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, BK1080_SCL_PIN);
    gpio_bit_set(BK4819_CS_PORT, BK4819_CS_PIN);
    gpio_mode_set(BK4819_CS_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, BK4819_CS_PIN);
    gpio_bit_reset(BK4819_PORT, BK4819_SCK_PIN | BK4819_SDA_PIN);
    gpio_mode_set(BK4819_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, BK4819_SCK_PIN | BK4819_SDA_PIN);
    gpio_output_options_set(BK4819_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_10MHZ, BK4819_SCK_PIN | BK4819_SDA_PIN);

    static const uint16_t seq1[][2] = {
        {0x37, 0x1D0F}, {0x13, 0x03BE}, {0x12, 0x037B}, {0x11, 0x027B}, {0x10, 0x007A},
        {0x14, 0x0019}, {0x49, 0x2A38}, {0x7B, 0x8420}, {0x7D, 0xE952}, {0x48, 0xB3FF},
        {0x1E, 0x4C58}, {0x1F, 0xA656}, {0x3E, 0xA037}, {0x3F, IRQ_ENABLE}, {0x2A, 0x7FFF},
        {0x28, 0x6B00}, {0x53, 0xE678}, {0x2C, 0x5705}, {0x4B, 0x7102},
    };
    static const uint16_t seq2[][2] = {
        {0x77, 0x88EF}, {0x26, 0x13A0}, {0x4E, 0x6F15}, {0x4F, 0x3F3E},
        {0x09, 0x006F}, {0x09, 0x106B}, {0x09, 0x2067}, {0x09, 0x3062}, {0x09, 0x4050},
        {0x09, 0x5047}, {0x09, 0x603A}, {0x09, 0x702C}, {0x09, 0x8041}, {0x09, 0x9037},
        {0x09, 0xA025}, {0x09, 0xB017}, {0x09, 0xC0E4}, {0x09, 0xD0CB}, {0x09, 0xE0B5},
        {0x09, 0xF09F}, {0x74, 0xFA02}, {0x44, 0x8F88}, {0x45, 0x3201},
    };
    bk4819_write(0x00, 0x8000);                  /* soft reset */
    delay_ms(2);
    bk4819_write(0x00, 0x0000);
    for (unsigned i = 0; i < sizeof seq1 / sizeof seq1[0]; i++)
        bk4819_write((uint8_t)seq1[i][0], seq1[i][1]);
    bk4819_write(0x40, (uint16_t)((bk4819_read(0x40) & 0xF000) | 0x04D2));
    for (unsigned i = 0; i < sizeof seq2 / sizeof seq2[0]; i++)
        bk4819_write((uint8_t)seq2[i][0], seq2[i][1]);
}

static uint8_t fsk_modem = BK_FSK_OFF, fsk_rx_len, fsk_invert, fsk_tx_invert;
static uint8_t fsk_tx_off;                      /* bk4819_tx() switched the modem off */
static uint16_t pocsag_baud = 1200;
static uint16_t afsk_r58 = 0x3309;              /* FFSK 1200/2400 RX: the closest to 1200/2200 */
static void fsk_arm(void);

/* CTCSS / DCS, as stock (0x80014d4, 0x8001334, 0x800291c, measured with tools/re/call_stock.py):
   CTCSS: REG_51 0x9000, REG_07 = Hz x 20.64888 (CTC1); then REG_07 0x21CD and REG_52 0x0292 set the
   55 Hz tail detector (CTC2). DCS: REG_51 0x8000 (0xA000 sends the inverted code), REG_07 0x0AD7
   (134.4 bit/s), REG_08 the 23-bit Golay codeword in two halves. Receive without a tone keeps stock's
   55 Hz set-up (CTC1 0x0471); transmit without one has REG_51 0. Received tones show in REG_0C:
   bit 10 CTCSS, 14 DCS, 15 inverted DCS. */
static uint32_t golay(uint16_t code)
{
    uint32_t cw = 0x800U | code, w = cw;
    for (int i = 0; i < 12; i++) {
        w <<= 1;
        if (w & 0x1000)
            w ^= 0x08EA;
    }
    return cw | (w & 0x0FFE) << 11;
}

static void tone_regs(int type, uint16_t value, int tx)
{
    if (type == BK_TONE_DCS || type == BK_TONE_DCS_INV) {
        uint32_t cw = golay(value);
        bk4819_write(0x51, tx && type == BK_TONE_DCS_INV ? 0xA000 : 0x8000);
        bk4819_write(0x07, 0x0AD7);
        bk4819_write(0x08, (uint16_t)(cw & 0x0FFF));
        bk4819_write(0x08, (uint16_t)(0x8000 | (cw >> 12 & 0x0FFF)));
        return;
    }
    if (tx && type == BK_TONE_NONE)
        return;
    bk4819_write(0x51, 0x9000);
    bk4819_write(0x07, type == BK_TONE_CTCSS ? (uint16_t)(((uint32_t)value * 206488U + 50000U) / 100000U)
                                             : 0x0471);
    bk4819_write(0x07, 0x21CD);
    bk4819_write(0x52, 0x0292);
}

/* the BK4819's voice inversion scrambler (REG_31 bit 1, REG_71 its frequency), types 1 and 2 */
static void scramble_regs(int type)
{
    uint16_t r31 = bk4819_read(0x31);
    if (type) {
        bk4819_write(0x71, (uint16_t)(0x68DC + (type - 1) * 1032));
        bk4819_write(0x31, r31 | 2);
    } else if (r31 & 2) {
        bk4819_write(0x31, (uint16_t)(r31 & ~2));
    }
}

void bk4819_rx(uint32_t freq_10hz, int narrow, uint16_t reg78, uint16_t reg4f, int tone, uint16_t tone_value,
               int scramble)
{
    bk4819_write(0x37, 0x1F0F);
    bk4819_write(0x30, 0x0000);
    bk4819_write(0x38, (uint16_t)freq_10hz);
    bk4819_write(0x39, (uint16_t)(freq_10hz >> 16));
    bk4819_write(0x43, narrow ? 0x4048 : 0x3028);
    bk4819_write(0x78, reg78);                   /* squelch RSSI open << 8 | close */
    bk4819_write(0x4F, reg4f);                   /* squelch noise close << 8 | open */
    tone_regs(tone, tone_value, 0);
    scramble_regs(scramble);
    bk4819_write(0x30, 0x0000);
    bk4819_write(0x28, 0x6B38);                  /* RX audio expander, as stock 0x8004980 */
    bk4819_write(0x30, 0x0200);
    bk4819_write(0x30, 0xBFF1);                  /* RX: VCO cal, RX link, AF DAC, PLL, RX DSP */
    if (fsk_modem != BK_FSK_OFF && fsk_tx_off) {
        fsk_tx_off = 0;
        bk4819_fsk_setup(fsk_modem, fsk_rx_len);   /* back on after bk4819_tx() switched it off */
    } else if (fsk_modem != BK_FSK_OFF) {
        fsk_arm();                               /* a fresh FIFO on every frequency */
    }
}

void bk4819_off(void)
{
    bk4819_write(0x30, 0x0000);
}

/* Stock transmit start (0x8009b70, channel without tones), with the PA power (REG_36) written last:
   the caller has already switched the RF path to transmit. The FSK modem (left on for receiving
   messages) is switched off first: while it is on, the microphone doesn't reach the modulator and
   the radio sends a silent carrier. The UV-K5 messenger does the same (MSG_EnableRX(false) on TX).
   bk4819_fsk_send() sets it up again for a data packet; bk4819_rx() restores it for receiving. */
void bk4819_tx(uint32_t freq_10hz, int narrow, uint16_t reg36, int tone, uint16_t tone_value, int scramble)
{
    if (fsk_modem != BK_FSK_OFF) {
        bk4819_write(0x59, 0x0000);
        bk4819_write(0x70, 0x0000);
        bk4819_write(0x58, 0x0000);
        fsk_tx_off = 1;
    }
    bk4819_write(0x37, 0x1D0F);
    bk4819_write(0x30, 0x0000);
    bk4819_write(0x38, (uint16_t)freq_10hz);
    bk4819_write(0x39, (uint16_t)(freq_10hz >> 16));
    bk4819_write(0x43, narrow ? 0x4048 : 0x3028);
    bk4819_write(0x51, 0x0000);
    bk4819_write(0x52, 0x0000);
    tone_regs(tone, tone_value, 1);
    scramble_regs(scramble);
    bk4819_write(0x7D, (uint16_t)((bk4819_read(0x7D) & 0xFFE0) | 0x1A));   /* mic gain, as stock */
    bk4819_write(0x30, 0x0200);
    bk4819_write(0x30, 0xC1FE);                  /* TX: PLL, PA gain, mic ADC, TX DSP */
    bk4819_write(0x36, reg36);                   /* PA bias << 8 | gain */
}

void bk4819_pa_off(void)
{
    bk4819_write(0x36, 0x007F);                  /* stock 0x80088ca: bias 0 */
}

void bk4819_af(int type)
{
    static const uint16_t reg47[] = {0x6040, 0x6140, 0x6740};   /* mute, FM, AM */
    bk4819_write(0x47, reg47[type]);
}

int bk4819_rssi_dbm(void)
{
    return (int)(bk4819_read(0x67) & 0x01FF) / 2 - 160;
}

void bk4819_gpio(uint8_t mask, int on)
{
    uint16_t v = bk4819_read(0x33) & (uint16_t)~(mask << 8);   /* output enabled */
    bk4819_write(0x33, on ? (v | mask) : (v & (uint16_t)~mask));
}

/* Stock 0x8009bd8(0): tone 1 to the local speaker only (REG_30 mode 3: no PLL, no PA). Mode 4,
   which stock uses with a non-zero argument, transmits the tone and is not used here. */
void bk4819_beep(int on, uint16_t hz)
{
    if (on) {
        bk4819_write(0x71, (uint16_t)((uint32_t)hz * 103244U / 10000U));   /* Hz * 10.3244 */
        bk4819_write(0x70, (uint16_t)((bk4819_read(0x70) & ~0x8F00) | 0xBC00));
        bk4819_write(0x30, 0x0000);
        bk4819_write(0x30, 0x0302);
        bk4819_write(0x47, 0x6340);
        bk4819_write(0x48, 0xB8F5);
    } else {
        bk4819_write(0x70, 0x0000);
        bk4819_write(0x48, 0xB3FF);
    }
}

/* Frequency counter (stock 0x8009512 / 0x8009544 / 0x8009434): FSK, tones and interrupts off,
   then REG_32 starts a measurement of the strongest nearby carrier. */
void bk4819_search(int on)
{
    if (on) {
        bk4819_write(0x3F, 0x0000);
        bk4819_write(0x59, 0x2028);
        bk4819_write(0x70, 0x0000);
        bk4819_write(0x58, 0x0000);
        bk4819_write(0x32, 0x8245);
    } else {
        bk4819_write(0x32, 0x8244);
        bk4819_write(0x3F, IRQ_ENABLE);
        if (fsk_modem != BK_FSK_OFF)
            bk4819_fsk_setup(fsk_modem, fsk_rx_len);   /* the counter cleared REG_58/59/70 */
    }
}

/* 1 and the measured frequency (10 Hz units) once a measurement is complete; then restart it. */
int bk4819_search_result(uint32_t *freq_10hz)
{
    uint16_t hi = bk4819_read(0x0D);
    if (hi & 0x8000)
        return 0;
    *freq_10hz = ((uint32_t)(hi & 0x07FF) << 16) | bk4819_read(0x0E);
    bk4819_write(0x32, 0x8244);
    bk4819_write(0x32, 0x8245);
    return 1;
}

int bk4819_squelch_open(void)
{
    return (bk4819_read(0x0C) & 0x0002) != 0;
}

uint16_t bk4819_irq(void)
{
    if (!(bk4819_read(0x0C) & 0x0001))
        return 0;
    bk4819_write(0x02, 0x0000);
    return bk4819_read(0x02);
}

/* ---- FSK modem: the UV-K5 messenger's settings (kamilsss655 firmware, app/messenger.c) ---------
 * REG_70 tone 2 on at gain 96; REG_72 the bit rate as a tone-2 frequency word; REG_58 FFSK
 * 1200/1800 for 1200 bit/s, plain FSK for 700 and 450; sync bytes 30 72 57 6C (4-byte sync); no
 * CRC (REG_5C 0x5625); 16 preamble bytes when sending. */
static uint16_t fsk_rate(void)
{
    static const uint16_t rate[] = {1200, 700, 450, 1200};
    return fsk_modem == BK_FSK_POCSAG ? pocsag_baud : rate[fsk_modem];
}

/* BK_FSK_AFSK, for APRS (Bell 202, 1200/2200 Hz) receive: the chip has no Bell 202 demodulator, so
   it runs its FFSK one and hands over the raw tone decisions; the sync word is four HDLC flags as
   tones (seven bit times of one tone, one of the other). Which tone is which depends on where the
   sender's NRZI started, so the caller alternates bk4819_fsk_invert() until a packet syncs. */
/* BK_FSK_POCSAG: direct FSK (no tones) at pocsag_baud; receive syncs on the POCSAG frame sync
   codeword 7CD215D8. When sending, the sync word is 0xAA bytes too: the chip's 16 preamble bytes,
   these 4 and the caller's own make up POCSAG's 576-bit preamble, and the payload carries the
   codewords, sync codewords included. */
static void fsk_regs(int tx, int len)
{
    static const uint16_t r58[] = {0x3F03, 0x0301, 0x0301, 0};
    uint16_t sync_a = 0x3072, sync_b = 0x576C, reg58;
    uint16_t r59 = (uint16_t)((tx ? 15u << 4 : 0) | 1u << 3 |       /* preamble length, 4-byte sync */
                              (tx && fsk_tx_invert ? 1u << 9 : 0));
    if (fsk_modem == BK_FSK_AFSK) {
        reg58 = afsk_r58;
        sync_a = sync_b = 0xFEFE;
    } else if (fsk_modem == BK_FSK_POCSAG) {
        reg58 = (uint16_t)(3u << 8 | 3u << 4 | (pocsag_baud > 1200 ? 4u << 1 : 0) | 1);  /* preamble AA */
        sync_a = tx ? 0xAAAA : 0x7CD2;
        sync_b = tx ? 0xAAAA : 0x15D8;
    } else {
        reg58 = r58[fsk_modem];
    }
    bk4819_write(0x70, 0x00E0);
    bk4819_write(0x72, (uint16_t)(((uint32_t)fsk_rate() * 103244U + 5000) / 10000));
    bk4819_write(0x58, reg58);
    bk4819_write(0x5A, sync_a);
    bk4819_write(0x5B, sync_b);
    bk4819_write(0x5C, 0x5625);
    bk4819_write(0x5E, tx ? 16u << 3 : 64u << 3 | 1);   /* almost empty at 16 / almost full at 1 word */
    bk4819_write(0x5D, (uint16_t)(len << 8));
    bk4819_write(0x59, r59 | 0xC000);           /* clear both FIFOs */
    bk4819_write(0x59, r59);
    bk4819_write(0x02, 0x0000);
}

static void fsk_arm(void)
{
    uint16_t r59 = (uint16_t)(1u << 3 | (fsk_invert ? 1u << 10 : 0));
    bk4819_write(0x59, 0xC000 | r59);
    bk4819_write(0x59, 0x1000 | r59);           /* FSK receive on */
}

void bk4819_fsk_invert(int on)
{
    fsk_invert = (uint8_t)on;
    fsk_arm();
}

void bk4819_fsk_tx_invert(int on)
{
    fsk_tx_invert = (uint8_t)on;
}

void bk4819_pocsag_baud(uint16_t baud)
{
    pocsag_baud = baud;
    if (fsk_modem == BK_FSK_POCSAG)
        bk4819_fsk_setup(fsk_modem, fsk_rx_len);
}

void bk4819_afsk_rx_mode(uint16_t r58)
{
    afsk_r58 = r58;
    if (fsk_modem == BK_FSK_AFSK)
        bk4819_fsk_setup(fsk_modem, fsk_rx_len);
}

/* Tones on the transmitter in place of the microphone (UV-K5 BK4819_TransmitTone: tone 1 at gain
   66; REG_30 0xC3FA is stock's "TX of a generated tone"). bk4819_tone_hz() then changes the
   frequency without a phase jump. */
void bk4819_tone_tx(int on)
{
    static uint16_t r2b;
    if (on) {
        r2b = bk4819_read(0x2B);
        bk4819_write(0x70, 0x8000 | 66u << 8);
        bk4819_write(0x2B, 0x0005);             /* no TX 300 Hz high-pass, no pre-emphasis */
        bk4819_write(0x30, 0xC3FA);
    } else {
        bk4819_write(0x70, 0x0000);
        bk4819_write(0x2B, r2b);
        if (fsk_modem != BK_FSK_OFF)
            fsk_regs(0, fsk_rx_len);
    }
}

void bk4819_tone_hz(uint16_t hz)
{
    bk4819_write(0x71, (uint16_t)(((uint32_t)hz * 103244U + 5000) / 10000));
}

void bk4819_fsk_setup(int modem, int rx_len)
{
    fsk_modem = (uint8_t)modem;
    fsk_rx_len = (uint8_t)rx_len;
    if (modem == BK_FSK_OFF) {
        bk4819_write(0x59, 0x0000);
        bk4819_write(0x70, 0x0000);
        bk4819_write(0x58, 0x0000);
        return;
    }
    fsk_regs(0, rx_len);
    fsk_arm();
}

int bk4819_fsk_send(const uint8_t *data, int len, int narrow)
{
    uint16_t r40 = bk4819_read(0x40), r2b = bk4819_read(0x2B);
    int sent = 0;
    if (fsk_modem == BK_FSK_OFF)
        return 0;
    bk4819_write(0x30, (uint16_t)(bk4819_read(0x30) & ~(1u << 2)));   /* microphone off */
    bk4819_write(0x40, (uint16_t)((r40 & 0xF000) | (narrow ? 1200 : 1300)));    /* deviation */
    bk4819_write(0x2B, 0x0005);                 /* no TX 300 Hz high-pass, no pre-emphasis */
    fsk_regs(1, len);
    delay_ms(100);
    int i = 0;
    for (; i < len && i < 64; i += 2)           /* 32 words in the FIFO, the rest as it empties */
        bk4819_write(0x5F, (uint16_t)(data[i + 1] << 8 | data[i]));
    bk4819_write(0x59, (uint16_t)(0x0800 | 15u << 4 | 1u << 3 | (fsk_tx_invert ? 1u << 9 : 0)));
    uint32_t ms = (uint32_t)(16 + 4 + len) * 8 * 1000 / fsk_rate() + 300;
    for (uint32_t t = 0; t < ms && !sent; t++) {
        delay_ms(1);
        uint16_t f = bk4819_irq();
        if (f & 0x4000)                         /* FIFO almost empty: 16 more words */
            for (int k = 0; k < 16 && i < len; k++, i += 2)
                bk4819_write(0x5F, (uint16_t)(data[i + 1] << 8 | data[i]));
        sent = (f & 0x8000) != 0;               /* FSK TX finished */
    }
    delay_ms(100);
    fsk_regs(0, fsk_rx_len);
    bk4819_write(0x40, r40);
    bk4819_write(0x2B, r2b);
    return sent;
}

uint16_t bk4819_fsk_word(void)
{
    return bk4819_read(0x5F);
}

void bk4819_fsk_rearm(void)
{
    fsk_arm();
}
