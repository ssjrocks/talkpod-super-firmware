/* ST7735S 128 x 128 LCD on SPI1 (transmit only, mode 3), after Jamie793/A36Plus_Radio (MIT). */
#include "lcd.h"
#include "board.h"
#include "system.h"

#define CMD_SWRESET 0x01
#define CMD_SLPOUT  0x11
#define CMD_NORON   0x13
#define CMD_INVOFF  0x20
#define CMD_GAMSET  0x26
#define CMD_DISPON  0x29
#define CMD_CASET   0x2A
#define CMD_RASET   0x2B
#define CMD_RAMWR   0x2C
#define CMD_MADCTL  0x36
#define CMD_COLMOD  0x3A

/* Panel offsets inside the controller's 132 x 162 RAM. The stock firmware adds 32 to every column
   (its window function at 0x0800E200) and nothing to rows; the v0.3 test pattern confirmed it. */
#define X_OFFSET 32
#define Y_OFFSET 0
#define MADCTL   0xA8   /* Jamie's value: MY + MV + BGR */

static void spi_wait_idle(void)
{
    while (spi_i2s_flag_get(SPI1, SPI_FLAG_TBE) == RESET) {}
    while (spi_i2s_flag_get(SPI1, SPI_FLAG_TRANS) != RESET) {}
}

static void spi_byte(uint8_t b)
{
    while (spi_i2s_flag_get(SPI1, SPI_FLAG_TBE) == RESET) {}
    spi_i2s_data_transmit(SPI1, b);
}

static void cmd(uint8_t c)
{
    spi_wait_idle();
    gpio_bit_reset(LCD_PORT, LCD_DC_PIN);
    gpio_bit_reset(LCD_PORT, LCD_CS_PIN);
    spi_byte(c);
    spi_wait_idle();
    gpio_bit_set(LCD_PORT, LCD_DC_PIN);   /* parameters follow as data */
}

static void data(uint8_t d)
{
    spi_byte(d);
}

static void end(void)
{
    spi_wait_idle();
    gpio_bit_set(LCD_PORT, LCD_CS_PIN);
}

void lcd_backlight(int on)
{
    if (on)
        gpio_bit_set(LCD_PORT, LCD_BL_PIN);
    else
        gpio_bit_reset(LCD_PORT, LCD_BL_PIN);
}

void lcd_init(void)
{
    rcu_periph_clock_enable(RCU_GPIOB);
    rcu_periph_clock_enable(RCU_SPI1);

    gpio_af_set(LCD_PORT, GPIO_AF_0, LCD_SCK_PIN | LCD_SDA_PIN);
    gpio_mode_set(LCD_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, LCD_SCK_PIN | LCD_SDA_PIN);
    gpio_output_options_set(LCD_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, LCD_SCK_PIN | LCD_SDA_PIN);
    gpio_bit_set(LCD_PORT, LCD_CS_PIN | LCD_DC_PIN | LCD_RST_PIN);
    gpio_mode_set(LCD_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LCD_CS_PIN | LCD_DC_PIN | LCD_RST_PIN | LCD_BL_PIN);
    gpio_output_options_set(LCD_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ,
                            LCD_CS_PIN | LCD_DC_PIN | LCD_RST_PIN | LCD_BL_PIN);

    spi_parameter_struct p;
    spi_struct_para_init(&p);
    p.device_mode = SPI_MASTER;
    p.trans_mode = SPI_TRANSMODE_BDTRANSMIT;
    p.frame_size = SPI_FRAMESIZE_8BIT;
    p.nss = SPI_NSS_SOFT;
    p.clock_polarity_phase = SPI_CK_PL_HIGH_PH_2EDGE;
    p.prescale = SPI_PSC_4;               /* APB1 48 MHz / 4 = 12 MHz (ST7735S write limit ~15 MHz) */
    p.endian = SPI_ENDIAN_MSB;
    spi_i2s_deinit(SPI1);
    spi_init(SPI1, &p);
    spi_enable(SPI1);

    /* hardware reset */
    gpio_bit_set(LCD_PORT, LCD_RST_PIN);
    delay_ms(5);
    gpio_bit_reset(LCD_PORT, LCD_RST_PIN);
    delay_ms(20);
    gpio_bit_set(LCD_PORT, LCD_RST_PIN);
    delay_ms(120);

    cmd(CMD_SWRESET); end(); delay_ms(120);
    cmd(CMD_SLPOUT);  end(); delay_ms(120);
    cmd(CMD_COLMOD);  data(0x05); end();          /* 16 bits per pixel */
    cmd(CMD_MADCTL);  data(MADCTL); end();
    cmd(CMD_NORON);   end();
    cmd(CMD_INVOFF);  end();
    cmd(CMD_GAMSET);  data(0x01); end();
    lcd_fill(0, 0, LCD_W, LCD_H, 0x0000);
    cmd(CMD_DISPON);  end();
    lcd_backlight(1);
}

void lcd_begin(int x, int y, int w, int h)
{
    uint16_t x0 = (uint16_t)(x + X_OFFSET), x1 = (uint16_t)(x + w - 1 + X_OFFSET);
    uint16_t y0 = (uint16_t)(y + Y_OFFSET), y1 = (uint16_t)(y + h - 1 + Y_OFFSET);
    cmd(CMD_CASET); data(x0 >> 8); data(x0 & 0xFF); data(x1 >> 8); data(x1 & 0xFF); end();
    cmd(CMD_RASET); data(y0 >> 8); data(y0 & 0xFF); data(y1 >> 8); data(y1 & 0xFF); end();
    cmd(CMD_RAMWR);
}

void lcd_push(uint16_t color)
{
    spi_byte((uint8_t)(color >> 8));
    spi_byte((uint8_t)color);
}

void lcd_end(void)
{
    end();
}

void lcd_fill(int x, int y, int w, int h, uint16_t color)
{
    if (w <= 0 || h <= 0)
        return;
    lcd_begin(x, y, w, h);
    for (int n = w * h; n > 0; n--)
        lcd_push(color);
    lcd_end();
}
