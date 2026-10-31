/* BK1080 FM broadcast receiver, as stock (0x8000D34 start, 0x8000E94 tune, 0x8000F08 write): bit-banged
 * I2C on PF6 (SCL) and PA3 (SDA, shared with the BK4819, whose chip select stays high). Chip address
 * 0x80, then the register number << 1, then 16-bit values, high byte first. Stock sets PA8 when the
 * chip is up and clears it when leaving FM.
 */
#include "bk1080.h"
#include "board.h"
#include "system.h"

/* stock's table at 0x800E3D2: registers 0 to 33, high byte first */
static const uint16_t REGS[34] = {
    0x0008, 0x1080, 0x4201, 0x001D, 0x40C0, 0x0A5B, 0x002E, 0x02FF, 0x5B11, 0x0000, 0x411E, 0x0000,
    0xCE00, 0x0000, 0x0000, 0x1000, 0x3197, 0x0000, 0x13FF, 0x9852, 0x0000, 0x0000, 0x0008, 0x0000,
    0x51E1, 0xA8DC, 0x2645, 0x00E4, 0x1CD8, 0x3A50, 0xEAF0, 0x3000, 0x0000, 0x0000,
};

static void wait(void)
{
    for (volatile int i = 0; i < 30; i++) {}
}

static void scl(int on)
{
    if (on)
        gpio_bit_set(BK1080_SCL_PORT, BK1080_SCL_PIN);
    else
        gpio_bit_reset(BK1080_SCL_PORT, BK1080_SCL_PIN);
    wait();
}

static void sda(int on)
{
    if (on)
        gpio_bit_set(BK1080_SDA_PORT, BK1080_SDA_PIN);
    else
        gpio_bit_reset(BK1080_SDA_PORT, BK1080_SDA_PIN);
    wait();
}

static void put(uint8_t b)
{
    for (int i = 0; i < 8; i++, b = (uint8_t)(b << 1)) {
        sda(b & 0x80);
        scl(1);
        scl(0);
    }
    gpio_mode_set(BK1080_SDA_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, BK1080_SDA_PIN);   /* its ACK */
    wait();
    scl(1);
    scl(0);
    gpio_mode_set(BK1080_SDA_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, BK1080_SDA_PIN);
}

static void write_regs(uint8_t reg, const uint16_t *v, int n)
{
    sda(1);
    scl(1);
    sda(0);                                     /* start */
    scl(0);
    put(0x80);
    put((uint8_t)(reg << 1));
    for (int i = 0; i < n; i++) {
        put((uint8_t)(v[i] >> 8));
        put((uint8_t)v[i]);
    }
    sda(0);                                     /* stop */
    scl(1);
    sda(1);
    scl(0);                                     /* held low: SDA is also the BK4819's */
}

void bk1080_on(void)
{
    gpio_mode_set(BK1080_EN_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, BK1080_EN_PIN);
    write_regs(0, REGS, 34);
    delay_ms(25);
    gpio_bit_set(BK1080_EN_PORT, BK1080_EN_PIN);
}

void bk1080_off(void)
{
    static const uint16_t power_down = 0x0241;
    gpio_bit_reset(BK1080_EN_PORT, BK1080_EN_PIN);
    write_regs(2, &power_down, 1);
}

void bk1080_tune(uint16_t tenths)
{
    uint16_t off = (uint16_t)(tenths - 650), band = 0x0ADB, zero = 0;
    if (off >= 110) {                           /* from 76 MHz: the other band setting */
        band = 0x0A5B;
        off = (uint16_t)(off - 110);
    }
    write_regs(3, &zero, 1);
    write_regs(5, &band, 1);
    write_regs(3, &off, 1);
    off |= 0x8000;                              /* tune */
    write_regs(3, &off, 1);
}
