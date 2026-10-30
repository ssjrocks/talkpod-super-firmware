/* Board-level outputs found in the stock firmware (docs/stock-firmware.md): the RF path switches,
 * whose wiring depends on the board revision tag the bootloader carries, the LEDs, and the power
 * hold / power switch pair. */
#include <string.h>
#include "board.h"
#include "bk4819.h"
#include "debug.h"
#include "lcd.h"
#include "system.h"

static uint8_t hwver2;
static uint8_t switch_seen_on, off_scans;

int board_hwver2(void)
{
    return hwver2;
}

void board_init(void)
{
    hwver2 = memcmp((const void *)0x08000300U, "HWVER02", 7) == 0;
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_GPIOB);
    rcu_periph_clock_enable(RCU_GPIOC);
    rcu_periph_clock_enable(RCU_GPIOF);

    /* PA15 is left as the bootloader set it (output, low): stock never sets it, only clears it
       before its power-off reset. v0.6 drove it high, and no audio came out of the speaker. */
    gpio_mode_set(POWER_SENSE_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, POWER_SENSE_PIN);

    gpio_bit_reset(MIC_EN_PORT, MIC_EN_PIN);
    gpio_mode_set(MIC_EN_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, MIC_EN_PIN);
    gpio_bit_reset(LED_PORT, LED_RED_PIN | LED_GREEN_PIN);
    gpio_mode_set(LED_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LED_RED_PIN | LED_GREEN_PIN);
    gpio_bit_reset(RF_PB_PORT, RF_PB0_PIN | RF_PB1_PIN);
    gpio_mode_set(RF_PB_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, RF_PB0_PIN | RF_PB1_PIN);
    gpio_bit_reset(RF_PC_PORT, RF_PC14_PIN | RF_PC15_PIN);
    gpio_mode_set(RF_PC_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, RF_PC14_PIN | RF_PC15_PIN);
}

/* Stock 0x8008710. Band parity is what matters: even (below 300 MHz, airband included) uses one
   front end, odd (300 MHz and up) the other. On HWVER02 stock drives PB1 as TMR3 CH4 PWM (period
   256) and sets it to 255 for even-band RX, 0 when off; a steady high stands in for 255/256 here.
   TX: HWVER02 switches UHF with the BK4819's GPIO2 and VHF with PC15; other boards use PB0 / PC15.
   tx.c never transmits in the airband (stock band 4, which has no TX path at all). */
void board_rf_path(uint32_t freq_10hz, int mode)
{
    int uhf = freq_10hz >= 30000000U;
    uint32_t rx_even = hwver2 ? RF_PB0_PIN : RF_PB1_PIN;

    if (mode == RF_OFF) {
        bk4819_write(0x33, 0x0000);
        gpio_bit_reset(RF_PC_PORT, RF_PC14_PIN | RF_PC15_PIN);
        gpio_bit_reset(RF_PB_PORT, RF_PB0_PIN | RF_PB1_PIN);
    } else if (mode == RF_TX) {
        if (uhf) {
            gpio_bit_reset(RF_PC_PORT, RF_PC15_PIN);
            if (hwver2)
                bk4819_gpio(0x04, 1);
            else
                gpio_bit_set(RF_PB_PORT, RF_PB0_PIN);
        } else {
            if (hwver2)
                bk4819_gpio(0x04, 0);
            else
                gpio_bit_reset(RF_PB_PORT, RF_PB0_PIN);
            gpio_bit_set(RF_PC_PORT, RF_PC15_PIN);
        }
    } else if (uhf) {
        gpio_bit_reset(RF_PB_PORT, rx_even);
        gpio_bit_set(RF_PC_PORT, RF_PC14_PIN);
    } else {
        gpio_bit_reset(RF_PC_PORT, RF_PC14_PIN);
        gpio_bit_set(RF_PB_PORT, hwver2 ? (RF_PB0_PIN | RF_PB1_PIN) : rx_even);
    }
}

/* PA12 enables the speaker amplifier; like stock 0x800b6e0 it stays on while any user wants it */
void board_speaker(int who, int on)
{
    static uint8_t users;
    users = (uint8_t)(on ? users | who : users & ~who);
    if (users)
        gpio_bit_set(MIC_EN_PORT, MIC_EN_PIN);
    else
        gpio_bit_reset(MIC_EN_PORT, MIC_EN_PIN);
}

void board_led(int red, int green)
{
    if (red)
        gpio_bit_set(LED_PORT, LED_RED_PIN);
    else
        gpio_bit_reset(LED_PORT, LED_RED_PIN);
    if (green)
        gpio_bit_set(LED_PORT, LED_GREEN_PIN);
    else
        gpio_bit_reset(LED_PORT, LED_GREEN_PIN);
}

/* As stock: the switch reads off (high) for 50 ms -> clear PA15, and if we are still powered
   500 ms later (a charger), restart. Only after the switch has been seen on
   once, so a misread pin can't switch the radio off at every start. */
void board_power_poll(void)
{
    if (gpio_input_bit_get(POWER_SENSE_PORT, POWER_SENSE_PIN) == RESET) {
        switch_seen_on = 1;
        off_scans = 0;
        return;
    }
    if (!switch_seen_on || ++off_scans < 5)
        return;
    debug_puts("power switch off\r\n");
    lcd_backlight(0);
    bk4819_off();
    board_rf_path(0, RF_OFF);
    board_led(0, 0);
    gpio_bit_reset(POWER_HOLD_PORT, POWER_HOLD_PIN);
    for (uint32_t t0 = millis(); millis() - t0 < 500;)
        fwdgt_feed();
    system_reset();
}
