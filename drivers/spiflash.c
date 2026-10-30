/* W25Q16 on SPI0 (PA4 CS, PA5 SCK, PA6 MISO, PA7 MOSI, AF0), mode 0, 12 MHz. Writes are limited
   to FLASH_OWN_START..FLASH_OWN_END. */
#include "spiflash.h"
#include "board.h"
#include "system.h"

#define CMD_READ          0x03
#define CMD_JEDEC_ID      0x9F
#define CMD_WRITE_ENABLE  0x06
#define CMD_READ_STATUS   0x05
#define CMD_PAGE_PROGRAM  0x02
#define CMD_SECTOR_ERASE  0x20
#define PAGE              256U

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

static int own(uint32_t addr, uint32_t len)
{
    return FLASH_WRITES && addr >= FLASH_OWN_START && len <= FLASH_OWN_END - addr;
}

static void command(uint8_t cmd, uint32_t addr, int with_addr)
{
    select(1);
    xfer(cmd);
    if (with_addr) {
        xfer((uint8_t)(addr >> 16));
        xfer((uint8_t)(addr >> 8));
        xfer((uint8_t)addr);
    }
}

/* sector erase takes up to 400 ms, a page program up to 3 ms */
static int wait_ready(uint32_t timeout_ms)
{
    uint32_t t0 = millis();
    select(1);
    xfer(CMD_READ_STATUS);
    while (xfer(0) & 1) {
        if (millis() - t0 > timeout_ms) {
            select(0);
            return 0;
        }
        fwdgt_feed();
    }
    select(0);
    return 1;
}

int spiflash_erase(uint32_t addr)
{
    addr &= ~(FLASH_SECTOR - 1);
    if (!own(addr, FLASH_SECTOR))
        return 0;
    command(CMD_WRITE_ENABLE, 0, 0);
    select(0);
    command(CMD_SECTOR_ERASE, addr, 1);
    select(0);
    return wait_ready(1000);
}

int spiflash_write(uint32_t addr, const void *src, uint32_t len)
{
    const uint8_t *p = src;
    if (!own(addr, len))
        return 0;
    while (len) {
        uint32_t n = PAGE - (addr & (PAGE - 1));    /* a program must not cross a page */
        if (n > len)
            n = len;
        command(CMD_WRITE_ENABLE, 0, 0);
        select(0);
        command(CMD_PAGE_PROGRAM, addr, 1);
        for (uint32_t i = 0; i < n; i++)
            xfer(p[i]);
        select(0);
        if (!wait_ready(20))
            return 0;
        addr += n;
        p += n;
        len -= n;
    }
    return 1;
}
