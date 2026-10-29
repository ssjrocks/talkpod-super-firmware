#include "board.h"

void gpio_config(void)
{
    /* enable clocks for all used GPIO ports + AFIO */
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_GPIOB);
    rcu_periph_clock_enable(RCU_GPIOC);
    rcu_periph_clock_enable(RCU_AF);

    /* ── LCD ────────────────────────────────────────────────── */
    /* SPI1 pins: SCK (PB13), MOSI (PB15) — AF push-pull */
    gpio_init(LCD_PORT, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ,
              LCD_SCK_PIN | LCD_SDA_PIN);
    /* control pins: CS, RST, DC, backlight — output push-pull */
    gpio_init(LCD_PORT, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ,
              LCD_CS_PIN | LCD_RST_PIN | LCD_DC_PIN | LCD_BL_PIN);

    /* ── External flash (SPI0) ─────────────────────────────── */
    /* SPI0 pins: SCK (PA5), MISO (PA6), MOSI (PA7) — AF push-pull */
    gpio_init(FLASH_PORT, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ,
              FLASH_SCK_PIN | FLASH_MISO_PIN | FLASH_MOSI_PIN);
    /* CS (PA4) — output push-pull, start high */
    gpio_init(FLASH_PORT, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, FLASH_CS_PIN);
    gpio_bit_set(FLASH_PORT, FLASH_CS_PIN);

    /* ── BK4819 (bit-bang SPI) ─────────────────────────────── */
    gpio_init(BK4819_PORT, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ,
              BK4819_CLK_PIN | BK4819_DAT_PIN);
    gpio_init(BK4819_CS_PORT, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ,
              BK4819_CS_PIN);
    gpio_bit_set(BK4819_CS_PORT, BK4819_CS_PIN);

    /* ── BK1080 (bit-bang I2C) ─────────────────────────────── */
    gpio_init(BK1080_EN_PORT, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ,
              BK1080_EN_PIN);
    gpio_bit_reset(BK1080_EN_PORT, BK1080_EN_PIN);
    gpio_init(BK1080_SCL_PORT, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ,
              BK1080_SCL_PIN);

    /* ── Keypad ────────────────────────────────────────────── */
    /* rows: output push-pull */
    gpio_init(KEY_PORT, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ,
              KEY_ROW0_PIN | KEY_ROW1_PIN | KEY_ROW2_PIN | KEY_ROW3_PIN);
    /* columns: input pull-up */
    gpio_init(KEY_PORT, GPIO_MODE_IPU, GPIO_OSPEED_50MHZ,
              KEY_COL0_PIN | KEY_COL1_PIN | KEY_COL2_PIN | KEY_COL3_PIN);
    /* PTT: input pull-up */
    gpio_init(KEY_PTT_PORT, GPIO_MODE_IPU, GPIO_OSPEED_50MHZ, KEY_PTT_PIN);

    /* ── Battery ADC ───────────────────────────────────────── */
    gpio_init(BATT_PORT, GPIO_MODE_AIN, GPIO_OSPEED_50MHZ, BATT_PIN);

    /* ── MIC enable ────────────────────────────────────────── */
    gpio_init(MIC_EN_PORT, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, MIC_EN_PIN);
    gpio_bit_reset(MIC_EN_PORT, MIC_EN_PIN);
}
