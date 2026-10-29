#include "gd32f30x.h"
#include "system.h"

#define BOOT_CLOCK 48000000U

static volatile uint32_t ticks;

void systick_init(void)
{
    SystemCoreClock = BOOT_CLOCK;
    if (SysTick_Config(SystemCoreClock / 1000U))
        while (1);
    NVIC_SetPriority(SysTick_IRQn, 0x0FU);
}

void SysTick_Handler(void)
{
    ticks++;
}

uint32_t millis(void)
{
    return ticks;
}

void delay_ms(uint32_t ms)
{
    uint32_t t0 = ticks;
    while ((ticks - t0) < ms)
        __WFI();
}

void fwdgt_feed(void)
{
    FWDGT_CTL = 0x0000AAAAU;
}
