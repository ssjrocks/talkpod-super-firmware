#include "board.h"
#include "st7735s.h"
#include "system.h"

/* ST7735S commands */
#define CMD_NOP     0x00
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

/* pin helpers */
#define CS_LOW()   gpio_bit_reset(LCD_PORT, LCD_CS_PIN)
#define CS_HIGH()  gpio_bit_set(LCD_PORT, LCD_CS_PIN)
#define DC_LOW()   gpio_bit_reset(LCD_PORT, LCD_DC_PIN)
#define DC_HIGH()  gpio_bit_set(LCD_PORT, LCD_DC_PIN)
#define RST_LOW()  gpio_bit_reset(LCD_PORT, LCD_RST_PIN)
#define RST_HIGH() gpio_bit_set(LCD_PORT, LCD_RST_PIN)

static void spi_wait_idle(void)
{
    while (spi_i2s_flag_get(SPI1, SPI_FLAG_TRANS) != RESET)
        ;
}

static void spi_tx_byte(uint8_t b)
{
    while (spi_i2s_flag_get(SPI1, SPI_FLAG_TBE) == RESET)
        ;
    spi_i2s_data_transmit(SPI1, b);
}

static void lcd_cmd(uint8_t cmd)
{
    spi_wait_idle();
    DC_LOW();
    CS_LOW();
    spi_tx_byte(cmd);
    spi_wait_idle();
    CS_HIGH();
}

static void lcd_data(uint8_t d)
{
    spi_wait_idle();
    DC_HIGH();
    CS_LOW();
    spi_tx_byte(d);
    spi_wait_idle();
    CS_HIGH();
}

void lcd_set_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    lcd_cmd(CMD_CASET);
    lcd_data(x1 >> 8); lcd_data(x1 & 0xFF);
    lcd_data(x2 >> 8); lcd_data(x2 & 0xFF);

    lcd_cmd(CMD_RASET);
    lcd_data(y1 >> 8); lcd_data(y1 & 0xFF);
    lcd_data(y2 >> 8); lcd_data(y2 & 0xFF);

    lcd_cmd(CMD_RAMWR);
}

void lcd_fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    uint8_t hi = color >> 8, lo = color & 0xFF;

    lcd_set_window(x, y, x + w - 1, y + h - 1);

    DC_HIGH();
    CS_LOW();
    for (uint32_t n = (uint32_t)w * h; n > 0; n--) {
        spi_tx_byte(hi);
        spi_tx_byte(lo);
    }
    spi_wait_idle();
    CS_HIGH();
}

void lcd_fill_screen(uint16_t color)
{
    lcd_fill(0, 0, LCD_W, LCD_H, color);
}

void lcd_backlight(int on)
{
    if (on)
        gpio_bit_set(LCD_PORT, LCD_BL_PIN);
    else
        gpio_bit_reset(LCD_PORT, LCD_BL_PIN);
}

static void spi1_init(void)
{
    rcu_periph_clock_enable(RCU_SPI1);

    spi_parameter_struct cfg;
    spi_i2s_deinit(SPI1);
    spi_struct_para_init(&cfg);

    cfg.nss                  = SPI_NSS_SOFT;
    cfg.prescale             = SPI_PSC_2;       /* APB1=60 MHz /2 = 30 MHz */
    cfg.endian               = SPI_ENDIAN_MSB;
    cfg.device_mode          = SPI_MASTER;
    cfg.frame_size           = SPI_FRAMESIZE_8BIT;
    cfg.trans_mode           = SPI_TRANSMODE_BDTRANSMIT;
    cfg.clock_polarity_phase = SPI_CK_PL_HIGH_PH_2EDGE;
    spi_init(SPI1, &cfg);
    spi_enable(SPI1);
}

void lcd_init(void)
{
    spi1_init();

    /* hardware reset */
    RST_HIGH();
    delay_ms(10);
    RST_LOW();
    delay_ms(10);
    RST_HIGH();
    delay_ms(120);

    /* software reset */
    lcd_cmd(CMD_SWRESET);
    delay_ms(120);

    lcd_cmd(CMD_SLPOUT);
    delay_ms(120);

    lcd_cmd(CMD_NORON);
    lcd_cmd(CMD_INVOFF);

    /* gamma */
    lcd_cmd(CMD_GAMSET);
    lcd_data(0x01);

    /* pixel format: RGB565 */
    lcd_cmd(CMD_COLMOD);
    lcd_data(0x05);

    /* memory access: landscape 161×128, MADCTL = 0xA8 */
    lcd_cmd(CMD_MADCTL);
    lcd_data(0xA8);

    lcd_cmd(CMD_DISPON);
    delay_ms(20);

    lcd_fill_screen(0x0000);
    lcd_backlight(1);
}
