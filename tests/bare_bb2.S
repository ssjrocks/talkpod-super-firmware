  .syntax unified
  .cpu cortex-m4
  .thumb

.equ RCU_BASE,      0x40021000
.equ RCU_AHBEN,     RCU_BASE + 0x14
.equ RCU_CFG0,      RCU_BASE + 0x04

.equ GPIOA_BASE,    0x48000000
.equ GPIOA_MODER,   GPIOA_BASE + 0x00
.equ GPIOA_OTYPER,  GPIOA_BASE + 0x04
.equ GPIOA_OSPEEDR, GPIOA_BASE + 0x08
.equ GPIOA_PUPDR,   GPIOA_BASE + 0x0C
.equ GPIOA_IDR,     GPIOA_BASE + 0x10
.equ GPIOA_ODR,     GPIOA_BASE + 0x14
.equ GPIOA_BSRR,    GPIOA_BASE + 0x18
.equ GPIOB_BASE,    0x48000400
.equ GPIOB_MODER,   GPIOB_BASE + 0x00
.equ GPIOB_BSRR,    GPIOB_BASE + 0x18

.equ FWDGT_CTL,     0x40003000

  .section .isr_vector, "a", %progbits
  .type vectors, %object
vectors:
  .word 0x20003E48
  .word reset_handler
  .word hang
  .word hardfault_handler
  .word hang
  .word hang
  .word hang
  .size vectors, . - vectors

  .section .text, "ax", %progbits

  .thumb_func
  .global reset_handler
  .type reset_handler, %function
reset_handler:
  ldr r0, =FWDGT_CTL
  movw r1, #0xAAAA
  str r1, [r0]

  /* Enable GPIOA + GPIOB on AHB */
  ldr r0, =RCU_AHBEN
  ldr r1, [r0]
  orr r1, r1, #(1 << 17)
  orr r1, r1, #(1 << 18)
  str r1, [r0]

  movs r2, #64
1:  subs r2, r2, #1
  bne 1b

  /* PB7 output (backlight) — blink encodes RCU_CFG0 value */
  ldr r0, =GPIOB_MODER
  ldr r1, [r0]
  bic r1, r1, #(3 << 14)
  orr r1, r1, #(1 << 14)
  str r1, [r0]
  ldr r0, =GPIOB_BSRR
  mov r1, #(1 << 7)
  str r1, [r0]

  /* PA9: GPIO output, push-pull, high speed, no pull */
  ldr r0, =GPIOA_MODER
  ldr r1, [r0]
  bic r1, r1, #(3 << 18)
  orr r1, r1, #(1 << 18)    /* 01 = output */
  str r1, [r0]

  ldr r0, =GPIOA_OTYPER
  ldr r1, [r0]
  bic r1, r1, #(1 << 9)     /* 0 = push-pull */
  str r1, [r0]

  ldr r0, =GPIOA_OSPEEDR
  ldr r1, [r0]
  orr r1, r1, #(3 << 18)    /* 11 = very high speed */
  str r1, [r0]

  ldr r0, =GPIOA_PUPDR
  ldr r1, [r0]
  bic r1, r1, #(3 << 18)    /* 00 = no pull */
  str r1, [r0]

  /* PA9 HIGH (UART idle) */
  ldr r0, =GPIOA_BSRR
  mov r1, #(1 << 9)
  str r1, [r0]

  /* Read RCU_CFG0 and store for blink encoding later */
  ldr r0, =RCU_CFG0
  ldr r8, [r0]              /* r8 = RCU_CFG0 value for later */

  /* Long idle before first transmission */
  ldr r0, =10000000
  subs r0, r0, #1
  bne .-2

  /* === Bit-bang serial: send RCU_CFG0 as hex, then 0x55 pattern === */

  /* First: try 9600 baud assuming various clocks.
     We'll use delay=5625 which targets 9600@108MHz with 2 cycles/iter
     or ~6400 baud with 3 cycles/iter.
     But let's also try with delay calibrated from the 20M blink.
     The blink loop is 20M iters. If blink phase is ~556ms:
       cycles_per_phase = clock * 0.556
       cycles_per_iter = cycles_per_phase / 20M = clock * 0.556 / 20M
     For 9600 baud: bit_time = 1/9600 = 104.17us = clock * 104.17e-6 cycles
       delay_iters = (clock * 104.17e-6) / cycles_per_iter
                   = (clock * 104.17e-6) / (clock * 0.556 / 20M)
                   = 104.17e-6 * 20M / 0.556
                   = 2083400 / 0.556
                   = 3747

     So regardless of clock speed, 3747 iterations should give 9600 baud
     IF the blink is truly ~556ms. Let's use 3750.

     But since blink might not be exactly 556ms, also send at a much
     slower rate (1200 baud, delay=30000) as a fallback. */

.equ FAST_DELAY, 3750    /* targets 9600 baud */
.equ SLOW_DELAY, 30000   /* targets 1200 baud */

send_loop:
  ldr r0, =FWDGT_CTL
  movw r1, #0xAAAA
  str r1, [r0]

  /* PB7 ON */
  ldr r0, =GPIOB_BSRR
  mov r1, #(1 << 7)
  str r1, [r0]

  /* --- Send at SLOW rate (1200 baud): "U" then RCU_CFG0 as 8 hex chars --- */
  /* First send 0x55 at slow rate */
  movs r6, #0x55
  ldr r9, =SLOW_DELAY
  bl bb_send_byte_r9

  /* Send each nibble of RCU_CFG0 (r8) as ASCII hex, MSB first */
  movs r10, #28            /* bit shift: start at bit 28 (nibble 7) */
hex_loop:
  lsr r6, r8, r10
  and r6, r6, #0xF
  cmp r6, #10
  blt hex_digit
  add r6, r6, #('A' - 10)
  b hex_send
hex_digit:
  add r6, r6, #'0'
hex_send:
  bl bb_send_byte_r9
  subs r10, r10, #4
  bpl hex_loop

  /* Send CR LF */
  movs r6, #0x0D
  bl bb_send_byte_r9
  movs r6, #0x0A
  bl bb_send_byte_r9

  /* --- Now send at FAST rate (9600 baud): 8 x 0x55 + "ALIVE\r\n" --- */
  movs r10, #8
  ldr r9, =FAST_DELAY
fast_U:
  movs r6, #0x55
  bl bb_send_byte_r9
  subs r10, r10, #1
  bne fast_U

  adr r4, msg
  movs r10, #7
fast_msg:
  ldrb r6, [r4]
  adds r4, r4, #1
  bl bb_send_byte_r9
  subs r10, r10, #1
  bne fast_msg

  /* Delay ~556ms */
  ldr r0, =20000000
3:  subs r0, r0, #1
  bne 3b

  /* PB7 OFF */
  ldr r0, =GPIOB_BSRR
  mov r1, #(1 << 23)
  str r1, [r0]

  ldr r0, =20000000
4:  subs r0, r0, #1
  bne 4b

  b send_loop
  .size reset_handler, . - reset_handler

  /* Subroutine: bit-bang one byte in r6 using delay in r9
     Clobbers r0, r1, r2, r3, r11 */
  .thumb_func
  .type bb_send_byte_r9, %function
bb_send_byte_r9:
  push {lr}

  /* Start bit: PA9 LOW */
  ldr r0, =GPIOA_BSRR
  mov r1, #(1 << 25)
  str r1, [r0]
  mov r2, r9
bb_sd:
  subs r2, r2, #1
  bne bb_sd

  /* 8 data bits, LSB first */
  movs r11, #8
bb_bit:
  tst r6, #1
  beq bb_lo
  ldr r0, =GPIOA_BSRR
  mov r1, #(1 << 9)
  str r1, [r0]
  b bb_bd
bb_lo:
  ldr r0, =GPIOA_BSRR
  mov r1, #(1 << 25)
  str r1, [r0]
bb_bd:
  mov r2, r9
bb_dd:
  subs r2, r2, #1
  bne bb_dd
  lsrs r6, r6, #1
  subs r11, r11, #1
  bne bb_bit

  /* Stop bit: PA9 HIGH */
  ldr r0, =GPIOA_BSRR
  mov r1, #(1 << 9)
  str r1, [r0]
  mov r2, r9
bb_sp:
  subs r2, r2, #1
  bne bb_sp
  /* Extra idle */
  mov r2, r9
bb_sp2:
  subs r2, r2, #1
  bne bb_sp2

  pop {pc}
  .size bb_send_byte_r9, . - bb_send_byte_r9

  .thumb_func
  .type hardfault_handler, %function
hardfault_handler:
hf_loop:
  b hf_loop
  .size hardfault_handler, . - hardfault_handler

  .thumb_func
  .type hang, %function
hang:
  b hang
  .size hang, . - hang

  .align 2
msg:
  .ascii "ALIVE\r\n"

  .section .rodata.markers, "a", %progbits
  .asciz "PROGRAMJC37U"
  .asciz "TalkpodA36Plus"
  .asciz " V0.0.1"
