  .syntax unified
  .cpu cortex-m4
  .thumb

/* GD32F330/F3x0 register addresses */
.equ RCU_BASE,     0x40021000
.equ RCU_AHBEN,    RCU_BASE + 0x14
.equ GPIOB_BASE,   0x48000400
.equ GPIOB_MODER,  GPIOB_BASE + 0x00
.equ GPIOB_BSRR,   GPIOB_BASE + 0x18

/* Also try F303 addresses as fallback */
.equ RCU_APB2EN,   RCU_BASE + 0x18
.equ GPIOB_F303,   0x40010C00
.equ GPIOB_F303_CTL0, GPIOB_F303 + 0x00
.equ GPIOB_F303_BOP,  GPIOB_F303 + 0x10

/* FWDGT - feed the watchdog in case it's enabled */
.equ FWDGT_CTL,    0x40003000
.equ FWDGT_FEED,   0xAAAA

  .section .isr_vector, "a", %progbits
  .type vectors, %object
vectors:
  .word 0x20003E48        /* exact stock SP */
  .word reset_handler     /* Reset handler */
  .word hang              /* NMI */
  .word hang              /* HardFault */
  .word hang              /* MemManage */
  .word hang              /* BusFault */
  .word hang              /* UsageFault */
  .size vectors, . - vectors

  .section .text, "ax", %progbits
  .thumb_func
  .global reset_handler
  .type reset_handler, %function
reset_handler:
  /* Feed watchdog immediately */
  ldr r0, =FWDGT_CTL
  ldr r1, =FWDGT_FEED
  str r1, [r0]

  /* === Try F330 path: enable GPIOB on AHB === */
  ldr r0, =RCU_AHBEN
  ldr r1, [r0]
  movs r2, #1
  lsls r2, r2, #18       /* bit 18 = GPIOBEN */
  orrs r1, r1, r2
  str r1, [r0]

  /* Also enable on APB2 in case it's F303 */
  ldr r0, =RCU_APB2EN
  ldr r1, [r0]
  movs r2, #8             /* bit 3 = IOPBEN */
  orrs r1, r1, r2
  str r1, [r0]

  /* Brief stabilization delay */
  movs r2, #32
1:  subs r2, r2, #1
  bne 1b

  /* === F330 path: GPIOB MODER PB7 = 01 (output) === */
  ldr r0, =GPIOB_MODER
  ldr r1, [r0]
  ldr r2, =0xFFFF3FFF    /* clear bits 15:14 */
  ands r1, r1, r2
  ldr r2, =0x00004000    /* set bit 14 = 01 (output) */
  orrs r1, r1, r2
  str r1, [r0]

  /* F330: set PB7 via BSRR */
  ldr r0, =GPIOB_BSRR
  movs r1, #1
  lsls r1, r1, #7        /* bit 7 */
  str r1, [r0]

  /* === F303 path: GPIOB CTL0 PB7 = output push-pull === */
  ldr r0, =GPIOB_F303_CTL0
  ldr r1, [r0]
  ldr r2, =0x0FFFFFFF    /* clear bits 31:28 */
  ands r1, r1, r2
  ldr r2, =0x30000000    /* CNF=00 MODE=11 (push-pull 50MHz) */
  orrs r1, r1, r2
  str r1, [r0]

  /* F303: set PB7 via BOP */
  ldr r0, =GPIOB_F303_BOP
  movs r1, #1
  lsls r1, r1, #7
  str r1, [r0]

  /* Halt — backlight should be ON */
halt:
  ldr r0, =FWDGT_CTL
  ldr r1, =FWDGT_FEED
  str r1, [r0]            /* keep feeding watchdog */
  b halt
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
