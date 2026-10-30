/* W25Q16 on SPI0 (PA4 CS, PA5 SCK, PA6 MISO, PA7 MOSI, AF0), mode 0, 12 MHz. Read-only. */
#include "spiflash.h"
#include "board.h"

#define CMD_READ      0x03
#define CMD_JEDEC_ID  0x9F

static uint8_t xfer(uint8_t b)
{
    while (spi_i2s_flag_get(SPI0, SPI_FLAG_TBE) == RESET) {}
    spi_i2s_data_transmit(SPI0, b);
    while (spi_i2s_flag_get(SPI0, SPI_FLAG_RBNE) == RESET) {}
    return (uint8_t)spi_i2s_data_receive(SPI0);
}

static void select(int on)
{
    if (on) {
        gpio_bit_reset(FLASH_PORT, FLASH_CS_PIN);
    } else {
        while (spi_i2s_flag_get(SPI0, SPI_FLAG_TRANS) != RESET) {}
        gpio_bit_set(FLASH_PORT, FLASH_CS_PIN);
    }
}

void spiflash_init(void)
{
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_SPI0);
    gpio_bit_set(FLASH_PORT, FLASH_CS_PIN);
    gpio_mode_set(FLASH_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, FLASH_CS_PIN);
    gpio_af_set(FLASH_PORT, GPIO_AF_0, FLASH_SCK_PIN | FLASH_MISO_PIN | FLASH_MOSI_PIN);
    gpio_mode_set(FLASH_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, FLASH_SCK_PIN | FLASH_MISO_PIN | FLASH_MOSI_PIN);
    gpio_output_options_set(FLASH_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, FLASH_SCK_PIN | FLASH_MOSI_PIN);

    spi_parameter_struct p;
    spi_struct_para_init(&p);
    p.device_mode = SPI_MASTER;
    p.trans_mode = SPI_TRANSMODE_FULLDUPLEX;
    p.frame_size = SPI_FRAMESIZE_8BIT;
    p.nss = SPI_NSS_SOFT;
    p.clock_polarity_phase = SPI_CK_PL_LOW_PH_1EDGE;
    p.prescale = SPI_PSC_4;                     /* APB2 48 MHz / 4 = 12 MHz */
    p.endian = SPI_ENDIAN_MSB;
    spi_i2s_deinit(SPI0);
    spi_init(SPI0, &p);
    spi_enable(SPI0);
}

uint32_t spiflash_jedec_id(void)
{
    select(1);
    xfer(CMD_JEDEC_ID);
    uint32_t id = (uint32_t)xfer(0) << 16;
    id |= (uint32_t)xfer(0) << 8;
    id |= xfer(0);
    select(0);
    return id;
}

void spiflash_read(uint32_t addr, void *dst, uint32_t len)
{
    uint8_t *p = dst;
    select(1);
    xfer(CMD_READ);
    xfer((uint8_t)(addr >> 16));
    xfer((uint8_t)(addr >> 8));
    xfer((uint8_t)addr);
    while (len--)
        *p++ = xfer(0);
    select(0);
}
