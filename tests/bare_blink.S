  .syntax unified
  .cpu cortex-m0
  .thumb

/* GD32F330 register addresses (AHB bus for GPIO, modern MODER layout) */
.equ RCU_BASE,     0x40021000
.equ RCU_AHBEN,    RCU_BASE + 0x14   /* AHB peripheral clock enable */
.equ RCU_AHBEN_GPIOBEN, (1 << 18)    /* GPIOB clock enable bit */

.equ GPIOB_BASE,   0x48000400        /* AHB bus, not APB2 */
.equ GPIOB_MODER,  GPIOB_BASE + 0x00 /* mode register (2 bits per pin) */
.equ GPIOB_BSRR,   GPIOB_BASE + 0x18 /* bit set/reset register */

.equ PB7_BIT,      (1 << 7)
.equ PB7_RESET_BIT, (1 << 23)        /* BSRR upper half = reset */
/* MODER: bits[15:14] for PB7. 01 = general purpose output */
.equ PB7_MODER_OUT,  (1 << 14)
.equ PB7_MODER_MSK,  (3 << 14)

  .section .isr_vector, "a", %progbits
  .type vectors, %object
vectors:
  .word 0x20004000        /* initial SP (top of 16KB SRAM) */
  .word reset_handler + 1 /* Reset handler (Thumb bit set) */
  .word hang + 1          /* NMI */
  .word hang + 1          /* HardFault */
  .size vectors, . - vectors

  .section .text, "ax", %progbits
  .thumb_func
  .global reset_handler
  .type reset_handler, %function
reset_handler:
  /* 1. Enable GPIOB clock on AHB */
  ldr r0, =RCU_AHBEN
  ldr r1, [r0]
  ldr r2, =RCU_AHBEN_GPIOBEN
  orrs r1, r2
  str r1, [r0]

  /* small delay for clock to stabilize */
  movs r2, #16
1:
  subs r2, r2, #1
  bne 1b

  /* 2. Configure PB7 as output via MODER (01 = output) */
  ldr r0, =GPIOB_MODER
  ldr r1, [r0]
  ldr r2, =PB7_MODER_MSK
  bics r1, r2
  ldr r2, =PB7_MODER_OUT
  orrs r1, r2
  str r1, [r0]

  /* 3. Blink loop using BSRR (set in low half, reset in high half) */
  ldr r3, =GPIOB_BSRR
  ldr r5, =PB7_BIT
  ldr r6, =PB7_RESET_BIT

blink:
  str r5, [r3]            /* PB7 = HIGH (backlight on) */
  ldr r0, =300000         /* delay ~300ms at 8 MHz IRC */
2:
  subs r0, r0, #1
  bne 2b

  str r6, [r3]            /* PB7 = LOW (backlight off) */
  ldr r0, =300000
3:
  subs r0, r0, #1
  bne 3b

  b blink
  .size reset_handler, . - reset_handler

  .thumb_func
  .type hang, %function
hang:
  b hang
  .size hang, . - hang

  .section .rodata.markers, "a", %progbits
  .asciz "PROGRAMJC37U"
  .asciz "TalkpodA36Plus"
  .asciz " V0.0.1"
