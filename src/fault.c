/* Fault reporting: any fault or unexpected interrupt prints where it happened over the serial
 * port, then keeps answering the programming handshake so talkpod-firmware can still flash a fix
 * without the side-key bootloader entry. */
#include <stdint.h>
#include "debug.h"
#include "progmode.h"

#define SCB_CFSR  (*(volatile uint32_t *)0xE000ED28U)
#define SCB_HFSR  (*(volatile uint32_t *)0xE000ED2CU)
#define SCB_MMFAR (*(volatile uint32_t *)0xE000ED34U)
#define SCB_BFAR  (*(volatile uint32_t *)0xE000ED38U)

void Fault_Report(void) __attribute__((naked));
void HardFault_Handler(void) __attribute__((alias("Fault_Report")));
void MemManage_Handler(void) __attribute__((alias("Fault_Report")));
void BusFault_Handler(void) __attribute__((alias("Fault_Report")));
void UsageFault_Handler(void) __attribute__((alias("Fault_Report")));

static void line(const char *name, uint32_t v)
{
    debug_puts(name);
    debug_hex32(v);
    debug_puts("\r\n");
}

void fault_dump(uint32_t *frame, uint32_t exc_return)
{
    uint32_t ipsr;
    __asm volatile ("mrs %0, ipsr" : "=r"(ipsr));
    debug_puts("\r\n!!! FAULT !!!\r\n");
    line("exception ", ipsr & 0x1FF);       /* 3 = HardFault, 4 MemManage, 5 BusFault, 6 UsageFault, 16+ = IRQ */
    line("pc        ", frame[6]);
    line("lr        ", frame[5]);
    line("xpsr      ", frame[7]);
    line("sp        ", (uint32_t)frame);
    line("exc_return", exc_return);
    line("cfsr      ", SCB_CFSR);
    line("hfsr      ", SCB_HFSR);
    line("mmfar     ", SCB_MMFAR);
    line("bfar      ", SCB_BFAR);
    for (;;) {
        *(volatile uint32_t *)0x40003000U = 0xAAAAU;   /* watchdog */
        progmode_poll();
    }
}

void Fault_Report(void)
{
    __asm volatile (
        "tst lr, #4      \n"
        "ite eq          \n"
        "mrseq r0, msp   \n"
        "mrsne r0, psp   \n"
        "mov r1, lr      \n"
        "b fault_dump    \n");
}
