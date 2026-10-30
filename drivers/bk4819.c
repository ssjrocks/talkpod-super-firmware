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
   RX finished (13) and TX finished (15) for the text messages. Nothing is wired to an MCU pin: the
   flags are polled in REG_0C bit 0 / REG_02. */
#define IRQ_ENABLE 0xB7FE

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

static uint8_t fsk_modem = BK_FSK_OFF, fsk_rx_len, fsk_invert;
static uint16_t afsk_r58 = 0x3309;              /* FFSK 1200/2400 RX: the closest to 1200/2200 */
static void fsk_arm(void);

void bk4819_rx(uint32_t freq_10hz, int narrow, uint16_t reg78, uint16_t reg4f)
{
    bk4819_write(0x37, 0x1F0F);
    bk4819_write(0x30, 0x0000);
    bk4819_write(0x38, (uint16_t)freq_10hz);
    bk4819_write(0x39, (uint16_t)(freq_10hz >> 16));
    bk4819_write(0x43, narrow ? 0x4048 : 0x3028);
    bk4819_write(0x78, reg78);                   /* squelch RSSI open << 8 | close */
    bk4819_write(0x4F, reg4f);                   /* squelch noise close << 8 | open */
    bk4819_write(0x51, 0x9000);
    bk4819_write(0x07, 0x0471);
    bk4819_write(0x07, 0x21CD);
    bk4819_write(0x52, 0x0292);
    bk4819_write(0x30, 0x0000);
    bk4819_write(0x28, 0x6B38);                  /* RX audio expander, as stock 0x8004980 */
    bk4819_write(0x30, 0x0200);
    bk4819_write(0x30, 0xBFF1);                  /* RX: VCO cal, RX link, AF DAC, PLL, RX DSP */
    if (fsk_modem != BK_FSK_OFF)
        fsk_arm();                               /* a fresh FIFO on every frequency */
}

void bk4819_off(void)
{
    bk4819_write(0x30, 0x0000);
}

/* Stock transmit start (0x8009b70, channel without tones), with the PA power (REG_36) written last:
   the caller has already switched the RF path to transmit. */
void bk4819_tx(uint32_t freq_10hz, int narrow, uint16_t reg36)
{
    bk4819_write(0x37, 0x1D0F);
    bk4819_write(0x30, 0x0000);
    bk4819_write(0x38, (uint16_t)freq_10hz);
    bk4819_write(0x39, (uint16_t)(freq_10hz >> 16));
    bk4819_write(0x43, narrow ? 0x4048 : 0x3028);
    bk4819_write(0x51, 0x0000);                  /* no CTCSS / DCS */
    bk4819_write(0x52, 0x0000);
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
static const uint16_t fsk_rate[] = {1200, 700, 450, 1200};

/* BK_FSK_AFSK, for APRS (Bell 202, 1200/2200 Hz) receive: the chip has no Bell 202 demodulator, so
   it runs its FFSK one and hands over the raw tone decisions; the sync word is four HDLC flags as
   tones (seven bit times of one tone, one of the other). Which tone is which depends on where the
   sender's NRZI started, so the caller alternates bk4819_fsk_invert() until a packet syncs. */
static void fsk_regs(int tx, int len)
{
    static const uint16_t tone2[] = {12389, 7227, 4646, 12389};
    int afsk = fsk_modem == BK_FSK_AFSK;
    uint16_t r59 = (uint16_t)((tx ? 15u << 4 : 0) | 1u << 3);   /* preamble length, 4-byte sync */
    bk4819_write(0x70, 0x00E0);
    bk4819_write(0x72, tone2[fsk_modem]);
    bk4819_write(0x58, afsk ? afsk_r58 : fsk_modem == BK_FSK_1200 ? 0x3F03 : 0x0301);
    bk4819_write(0x5A, afsk ? 0xFEFE : 0x3072);
    bk4819_write(0x5B, afsk ? 0xFEFE : 0x576C);
    bk4819_write(0x5C, 0x5625);
    if (!tx)
        bk4819_write(0x5E, 64u << 3 | 1);       /* FIFO almost full at 1 word */
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
    for (int i = 0; i < len; i += 2)
        bk4819_write(0x5F, (uint16_t)(data[i + 1] << 8 | data[i]));
    bk4819_write(0x59, 0x0800 | 15u << 4 | 1u << 3);                    /* send */
    uint32_t ms = (uint32_t)(16 + 4 + len) * 8 * 1000 / fsk_rate[fsk_modem] + 300;
    for (uint32_t t = 0; t < ms && !sent; t += 5) {
        delay_ms(5);
        sent = (bk4819_irq() & 0x8000) != 0;   /* FSK TX finished */
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
