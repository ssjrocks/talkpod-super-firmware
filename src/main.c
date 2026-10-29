#include "gd32f30x.h"
#include "board.h"
#include "system.h"
#include "debug.h"

const char __attribute__((used, section(".rodata.markers"))) fw_marker1[] = "PROGRAMJC37U";
const char __attribute__((used, section(".rodata.markers"))) fw_marker2[] = "TalkpodA36Plus";
const char __attribute__((used, section(".rodata.markers"))) fw_version[]  = " V0.2";

/* AHB GPIO registers — the actual hardware on this chip */
#define GPIOB_AHB_BASE  0x48000400U
#define GPIOB_MODER     (*(volatile uint32_t *)(GPIOB_AHB_BASE + 0x00))
#define GPIOB_BSRR      (*(volatile uint32_t *)(GPIOB_AHB_BASE + 0x18))

#define RCU_AHBEN_REG   (*(volatile uint32_t *)0x40021014U)

int main(void)
{
    fwdgt_feed();

    /* Enable GPIOB clock via AHB enable register (bit 18) */
    RCU_AHBEN_REG |= (1U << 18);
    for (volatile int i = 0; i < 16; i++);

    /* PB7 = output (MODER bits [15:14] = 01) */
    uint32_t moder = GPIOB_MODER;
    moder &= ~(3U << 14);
    moder |=  (1U << 14);
    GPIOB_MODER = moder;

    /* Backlight ON — proof of life */
    GPIOB_BSRR = (1U << 7);

    fwdgt_feed();

    debug_init(57600);
    debug_puts("\r\n=== A36SE FW v0.2 ===\r\n");
    debug_puts("Clock: 48 MHz (bootloader)\r\n");

    /* Which chip is this? (read-only information registers) */
    debug_puts("DENSITY:  "); debug_hex32(*(volatile uint32_t *)0x1FFFF7E0U);   /* [15:0] flash KB, [31:16] SRAM KB */
    debug_puts("\r\nDBG_ID:   "); debug_hex32(*(volatile uint32_t *)0xE0042000U);
    debug_puts("\r\nCPUID:    "); debug_hex32(*(volatile uint32_t *)0xE000ED00U);
    debug_puts("\r\nMVFR0:    "); debug_hex32(*(volatile uint32_t *)0xE000EF40U);  /* nonzero = FPU present */
    debug_puts("\r\nUID:      "); debug_hex32(*(volatile uint32_t *)0x1FFFF7ACU);
    debug_puts("\r\n");

    debug_puts("RCU_CTL:  ");
    debug_hex32(RCU_CTL);
    debug_puts("\r\nRCU_CFG0: ");
    debug_hex32(RCU_CFG0);
    debug_puts("\r\nRCU_AHBEN:");
    debug_hex32(RCU_AHBEN_REG);
    debug_puts("\r\n");

    fwdgt_feed();
    systick_init();
    __enable_irq();                 /* the startup code left interrupts masked */
    debug_puts("SysTick OK, interrupts on\r\n");

    uint32_t count = 0;
    while (1) {
        fwdgt_feed();

        /* toggle backlight */
        if (count & 1)
            GPIOB_BSRR = (1U << 7);       /* ON */
        else
            GPIOB_BSRR = (1U << (7+16));   /* OFF */

        delay_ms(500);
        count++;
        if ((count % 10) == 0) {
            debug_puts("beat ");
            debug_hex32(count);
            debug_puts("\r\n");
        }
    }
}
