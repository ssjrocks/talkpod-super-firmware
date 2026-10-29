#include "board.h"
#include "system.h"
#include "progmode.h"
#include "debug.h"

static volatile uint32_t ticks;

void systick_init(void)
{
    SysTick_Config(CPU_HZ / 1000U);          /* 1 ms */
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
    while ((ticks - t0) < ms) {
        progmode_poll();          /* keep answering the flasher while we wait */
        fwdgt_feed();
    }
}

void fwdgt_feed(void)
{
    FWDGT_CTL = 0x0000AAAAU;
}

void system_reset(void)
{
    debug_flush();
    __asm volatile ("dsb");
    *(volatile uint32_t *)0xE000ED0CU = 0x05FA0004U | (*(volatile uint32_t *)0xE000ED0CU & 0x700U);
    __asm volatile ("dsb");
    for (;;) {}
}
