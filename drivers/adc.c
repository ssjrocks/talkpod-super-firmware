/* ADC on the AT32F421, registers as the stock firmware programs them (0x800c708, 0x8006008): the
 * older STM32F1-style layout, so it is driven directly rather than through the GD32F3x0 library.
 * The ADC clock is left as the bootloader set it. */
#include "adc.h"
#include "board.h"
#include "spiflash.h"

#define ADC_STS   (*(volatile uint32_t *)0x40012400U)
#define ADC_CTRL2 (*(volatile uint32_t *)0x40012408U)
#define ADC_SPT2  (*(volatile uint32_t *)0x40012410U)
#define ADC_OSQ1  (*(volatile uint32_t *)0x4001242CU)
#define ADC_OSQ3  (*(volatile uint32_t *)0x40012434U)
#define ADC_ODT   (*(volatile uint32_t *)0x4001244CU)

#define STS_OCCE      (1U << 1)         /* conversion done */
#define CTRL2_ADON    (1U << 0)
#define CTRL2_CAL     (1U << 2)
#define CTRL2_RSTCAL  (1U << 3)
#define CTRL2_SWSEL   (7U << 17)        /* trigger: software */
#define CTRL2_TRIGEN  (1U << 20)
#define CTRL2_SWTRIG  (1U << 22)

static uint8_t thresholds[6];           /* battery levels in (reading >> 4) units */

void adc_init(void)
{
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_ADC);
    gpio_mode_set(GPIOA, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, GPIO_PIN_0 | BATTERY_PIN);
    ADC_CTRL2 = CTRL2_SWSEL;
    ADC_SPT2 = 0x3F;                    /* channels 0 and 1: longest sample time */
    ADC_OSQ1 = 0;                       /* one conversion */
    ADC_CTRL2 |= CTRL2_ADON;
    for (volatile int i = 0; i < 2000; i++) {}
    ADC_CTRL2 |= CTRL2_RSTCAL;
    for (int t = 100000; (ADC_CTRL2 & CTRL2_RSTCAL) && t; t--) {}
    ADC_CTRL2 |= CTRL2_CAL;
    for (int t = 100000; (ADC_CTRL2 & CTRL2_CAL) && t; t--) {}
    spiflash_read(0xF200, thresholds, sizeof thresholds);
}

uint16_t adc_read(int channel)
{
    ADC_OSQ3 = (uint32_t)channel;
    ADC_STS = 0;
    ADC_CTRL2 |= CTRL2_TRIGEN | CTRL2_SWTRIG;
    for (int t = 100000; !(ADC_STS & STS_OCCE) && t; t--) {}
    return (uint16_t)(ADC_ODT & 0x0FFF);
}

int battery_decivolts(void)
{
    return (adc_read(1) >> 4) * 482 / 1000;
}

int battery_bars(void)
{
    int v = adc_read(1) >> 4, bars = 0;
    for (int i = 1; i < 6; i++)
        if (v >= thresholds[i])
            bars = i;
    return bars;
}
