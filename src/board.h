/* Talkpod A36SE / A36plus 5 W board: pins (GD32F3x0-family MCU, LQFP-48).
 * From Jamie793/A36Plus_Radio Core/Inc/gpio.h and the stock firmware. */
#ifndef BOARD_H
#define BOARD_H

#include "gd32f3x0.h"

/* LCD: ST7735S, 128 x 128 (the boot logo is 128 x 128), SPI1 transmit-only */
#define LCD_PORT        GPIOB
#define LCD_CS_PIN      GPIO_PIN_2
#define LCD_BL_PIN      GPIO_PIN_7      /* backlight, high = on */
#define LCD_DC_PIN      GPIO_PIN_10     /* data/command ("WR" in Jamie's code) */
#define LCD_RST_PIN     GPIO_PIN_12
#define LCD_SCK_PIN     GPIO_PIN_13     /* SPI1_SCK, AF0 */
#define LCD_SDA_PIN     GPIO_PIN_15     /* SPI1_MOSI, AF0 */

/* External flash: W25Q16 (2 MB), SPI0 */
#define FLASH_PORT      GPIOA
#define FLASH_CS_PIN    GPIO_PIN_4
#define FLASH_SCK_PIN   GPIO_PIN_5      /* AF0 */
#define FLASH_MISO_PIN  GPIO_PIN_6      /* AF0 */
#define FLASH_MOSI_PIN  GPIO_PIN_7      /* AF0 */

/* BK4819 transceiver: bit-banged 3-wire SPI */
#define BK4819_PORT     GPIOA
#define BK4819_SCK_PIN  GPIO_PIN_2
#define BK4819_SDA_PIN  GPIO_PIN_3      /* bidirectional, shared with BK1080 SDA */
#define BK4819_CS_PORT  GPIOC
#define BK4819_CS_PIN   GPIO_PIN_13

/* BK1080 broadcast FM receiver: bit-banged I2C. Port F exists on this chip (the bootloader
   enables its clock), so Jamie's PF6 stands. */
#define BK1080_SCL_PORT GPIOF
#define BK1080_SCL_PIN  GPIO_PIN_6
#define BK1080_SDA_PORT GPIOA
#define BK1080_SDA_PIN  GPIO_PIN_3
#define BK1080_EN_PORT  GPIOA
#define BK1080_EN_PIN   GPIO_PIN_8

/* Keypad: 4 x 4 matrix, rows driven, columns read; PTT on the USART RX pin */
#define KEY_PORT        GPIOB
#define KEY_ROW0_PIN    GPIO_PIN_3
#define KEY_ROW1_PIN    GPIO_PIN_4
#define KEY_ROW2_PIN    GPIO_PIN_5
#define KEY_ROW3_PIN    GPIO_PIN_6
#define KEY_COL0_PIN    GPIO_PIN_11
#define KEY_COL1_PIN    GPIO_PIN_9
#define KEY_COL2_PIN    GPIO_PIN_14
#define KEY_COL3_PIN    GPIO_PIN_8
#define PTT_PORT        GPIOA
#define PTT_PIN         GPIO_PIN_10     /* also USART0 RX */

/* Misc */
#define BATTERY_PORT    GPIOA
#define BATTERY_PIN     GPIO_PIN_1      /* ADC */
#define MIC_EN_PORT     GPIOA
#define MIC_EN_PIN      GPIO_PIN_12

/* External flash map (stock layout, from Jamie793) */
#define EXT_FLASH_CHANNELS_ADDR 0x000000
#define EXT_FLASH_SPLASH_ADDR   0x07D000
#define EXT_FLASH_ICONS_ADDR    0x085140
#define EXT_FLASH_FONT8X16_ADDR 0x085910

/* Clocks: the bootloader leaves the PLL at 48 MHz (HXTAL 8 MHz / 2 x 12), AHB = APB1 = APB2. */
#define CPU_HZ          48000000U

#endif
