/* Talkpod A36SE / A36plus 5 W board: pins (Artery AT32F421C8T7, LQFP-48: DBG IDCODE 0x50020100).
 * The vendored GD32F3x0 library matches it for clocks, GPIO and SPI; its USART does not (see
 * src/debug.c). From Jamie793/A36Plus_Radio Core/Inc/gpio.h and the stock firmware. */
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

/* RF path switches, LEDs, power: see docs/stock-firmware.md (from the stock firmware) */
#define RF_PB_PORT      GPIOB
#define RF_PB0_PIN      GPIO_PIN_0
#define RF_PB1_PIN      GPIO_PIN_1
#define RF_PC_PORT      GPIOC
#define RF_PC14_PIN     GPIO_PIN_14
#define RF_PC15_PIN     GPIO_PIN_15
#define LED_PORT        GPIOA
#define LED_RED_PIN     GPIO_PIN_13     /* TX (assumed: the other one lights on RX) */
#define LED_GREEN_PIN   GPIO_PIN_14     /* lit by stock when a signal opens the squelch */
#define POWER_HOLD_PORT GPIOA
#define POWER_HOLD_PIN  GPIO_PIN_15     /* high keeps the radio powered */
#define POWER_SENSE_PORT GPIOF
#define POWER_SENSE_PIN GPIO_PIN_7      /* high = power switch turned off */

/* Misc */
#define BATTERY_PORT    GPIOA
#define BATTERY_PIN     GPIO_PIN_1      /* ADC */
#define MIC_EN_PORT     GPIOA
#define MIC_EN_PIN      GPIO_PIN_12
#define AUX_PA8_PIN     GPIO_PIN_8      /* stock: set with the BK1080 (FM broadcast) */
#define AUX_PA11_PIN    GPIO_PIN_11     /* stock: set when a voice prompt plays */

/* External flash map (stock layout, from Jamie793) */
#define EXT_FLASH_CHANNELS_ADDR 0x000000
#define EXT_FLASH_SPLASH_ADDR   0x07D000
#define EXT_FLASH_ICONS_ADDR    0x085140
#define EXT_FLASH_FONT8X16_ADDR 0x085910

/* Clocks: the bootloader leaves the PLL at 48 MHz (HXTAL 8 MHz / 2 x 12), AHB = APB1 = APB2. */
#define CPU_HZ          48000000U

/* src/board.c */
enum { RF_OFF, RF_RX };
void board_init(void);
int  board_hwver2(void);                    /* bootloader tag "HWVER02" at 0x08000300 */
void board_rf_path(uint32_t freq_10hz, int mode);
void board_led(int red, int green);
void board_power_poll(void);                /* every 10 ms: powers off when the switch is off */

#endif
