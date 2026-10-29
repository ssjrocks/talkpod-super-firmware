#ifndef BOARD_H
#define BOARD_H

#include "gd32f30x.h"

/* ── LCD (ST7735S on SPI1) ──────────────────────────────────── */
#define LCD_PORT        GPIOB
#define LCD_CS_PIN      GPIO_PIN_2
#define LCD_SDA_PIN     GPIO_PIN_15   /* SPI1 MOSI */
#define LCD_RST_PIN     GPIO_PIN_12
#define LCD_DC_PIN      GPIO_PIN_10   /* data/command (WR in Jamie's code) */
#define LCD_SCK_PIN     GPIO_PIN_13   /* SPI1 SCK */
#define LCD_BL_PIN      GPIO_PIN_7    /* backlight */

#define LCD_W           161
#define LCD_H           128

/* ── W25Q16JV external flash (SPI0) ─────────────────────────── */
#define FLASH_PORT      GPIOA
#define FLASH_CS_PIN    GPIO_PIN_4
#define FLASH_SCK_PIN   GPIO_PIN_5    /* SPI0 SCK */
#define FLASH_MISO_PIN  GPIO_PIN_6    /* SPI0 MISO */
#define FLASH_MOSI_PIN  GPIO_PIN_7    /* SPI0 MOSI */

/* ── BK4819 RF transceiver (bit-bang SPI) ───────────────────── */
#define BK4819_PORT     GPIOA
#define BK4819_CLK_PIN  GPIO_PIN_2
#define BK4819_DAT_PIN  GPIO_PIN_3    /* bidirectional */
#define BK4819_CS_PORT  GPIOC
#define BK4819_CS_PIN   GPIO_PIN_13

/* ── BK1080 FM receiver (bit-bang I2C) ──────────────────────── */
#define BK1080_SCL_PORT GPIOB         /* PF6 doesn't exist on LQFP-48 */
#define BK1080_SCL_PIN  GPIO_PIN_0    /* default guess — verify on hardware */
#define BK1080_SDA_PORT GPIOA
#define BK1080_SDA_PIN  GPIO_PIN_3    /* shared with BK4819 */
#define BK1080_EN_PORT  GPIOA
#define BK1080_EN_PIN   GPIO_PIN_8

/* ── Keypad (4×4 matrix) ────────────────────────────────────── */
#define KEY_PORT        GPIOB
#define KEY_ROW0_PIN    GPIO_PIN_3
#define KEY_ROW1_PIN    GPIO_PIN_4
#define KEY_ROW2_PIN    GPIO_PIN_5
#define KEY_ROW3_PIN    GPIO_PIN_6
#define KEY_COL0_PIN    GPIO_PIN_11
#define KEY_COL1_PIN    GPIO_PIN_9
#define KEY_COL2_PIN    GPIO_PIN_14
#define KEY_COL3_PIN    GPIO_PIN_8

#define KEY_PTT_PORT    GPIOA
#define KEY_PTT_PIN     GPIO_PIN_10   /* shared with USART0 RX */

/* ── USART0 (programming cable) ─────────────────────────────── */
#define USART_PORT      GPIOA
#define USART_TX_PIN    GPIO_PIN_9
#define USART_RX_PIN    GPIO_PIN_10   /* shared with PTT */

/* ── Battery ADC ────────────────────────────────────────────── */
#define BATT_PORT       GPIOA
#define BATT_PIN        GPIO_PIN_1    /* ADC01_IN1 */

/* ── Microphone enable ──────────────────────────────────────── */
#define MIC_EN_PORT     GPIOA
#define MIC_EN_PIN      GPIO_PIN_12

/* ── External flash address map ─────────────────────────────── */
#define EXT_FLASH_SPLASH_ADDR   0x07D000
#define EXT_FLASH_FONT8X16_ADDR 0x085910
#define EXT_FLASH_ICONS_ADDR    0x085140
#define EXT_FLASH_CHANNELS_ADDR 0x000000
#define EXT_FLASH_SETTINGS_ADDR 0x100000
#define EXT_FLASH_GIF_ADDR      0x110000

#endif /* BOARD_H */
