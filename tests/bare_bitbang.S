  .syntax unified
  .cpu cortex-m4
  .thumb

.equ RCU_BASE,      0x40021000
.equ RCU_AHBEN,     RCU_BASE + 0x14

.equ GPIOA_BASE,    0x48000000
.equ GPIOA_MODER,   GPIOA_BASE + 0x00
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

  /* PB7 output (backlight) */
  ldr r0, =GPIOB_MODER
  ldr r1, [r0]
  bic r1, r1, #(3 << 14)
  orr r1, r1, #(1 << 14)
  str r1, [r0]
  ldr r0, =GPIOB_BSRR
  mov r1, #(1 << 7)
  str r1, [r0]

  /* PA9 as GPIO output (NOT AF — we're bit-banging) */
  ldr r0, =GPIOA_MODER
  ldr r1, [r0]
  bic r1, r1, #(3 << 18)
  orr r1, r1, #(1 << 18)    /* 01 = output */
  str r1, [r0]

  /* PA9 idle HIGH (UART idle state) */
  ldr r0, =GPIOA_BSRR
  mov r1, #(1 << 9)
  str r1, [r0]

  /* Long idle to let receiver sync */
  ldr r0, =5000000
2:  subs r0, r0, #1
  bne 2b

  /* === Main loop: bit-bang "UUUUALIVE\r\n" at 9600 baud === */
  /* Calibration: blink loop is 20M iters for ~556ms.
     Assuming 3 cycles/iter: clock = 60M/0.556 = 108 MHz.
     9600 baud bit time = 1/9600 = 104.17 us = 11250 cycles at 108 MHz.
     Inner delay = 11250/3 = 3750 iterations.
     But timing may be off, so also try 2 cycles/iter (72 MHz):
     bit time = 7500 cycles, delay = 7500/3 = 2500.
     We'll use a moderate value and see. */

  /* Use 3750 iterations for ~104us at 108 MHz (9600 baud) */
.equ BIT_DELAY, 3750

send_loop:
  ldr r0, =FWDGT_CTL
  movw r1, #0xAAAA
  str r1, [r0]

  /* PB7 ON */
  ldr r0, =GPIOB_BSRR
  mov r1, #(1 << 7)
  str r1, [r0]

  /* Send 8 x 0x55 then "ALIVE\r\n" via bit-bang */
  adr r4, bb_msg
  movs r5, #15             /* 8 x 'U' + 7 bytes */
bb_send_byte:
  ldrb r6, [r4]
  adds r4, r4, #1

  /* Start bit: PA9 LOW */
  ldr r0, =GPIOA_BSRR
  mov r1, #(1 << 25)       /* reset bit 9 = PA9 low */
  str r1, [r0]
  ldr r2, =BIT_DELAY
bb_start_delay:
  subs r2, r2, #1
  bne bb_start_delay

  /* Data bits: LSB first */
  movs r7, #8
bb_data_bit:
  tst r6, #1
  beq bb_bit_low
  /* PA9 HIGH */
  ldr r0, =GPIOA_BSRR
  mov r1, #(1 << 9)
  str r1, [r0]
  b bb_bit_delay
bb_bit_low:
  /* PA9 LOW */
  ldr r0, =GPIOA_BSRR
  mov r1, #(1 << 25)
  str r1, [r0]
bb_bit_delay:
  ldr r2, =BIT_DELAY
bb_d_delay:
  subs r2, r2, #1
  bne bb_d_delay
  lsrs r6, r6, #1
  subs r7, r7, #1
  bne bb_data_bit

  /* Stop bit: PA9 HIGH */
  ldr r0, =GPIOA_BSRR
  mov r1, #(1 << 9)
  str r1, [r0]
  ldr r2, =BIT_DELAY
bb_stop_delay:
  subs r2, r2, #1
  bne bb_stop_delay

  /* Extra stop bit for safety */
  ldr r2, =BIT_DELAY
bb_stop2_delay:
  subs r2, r2, #1
  bne bb_stop2_delay

  subs r5, r5, #1
  bne bb_send_byte

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
bb_msg:
  .byte 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55
  .ascii "ALIVE\r\n"

  .section .rodata.markers, "a", %progbits
  .asciz "PROGRAMJC37U"
  .asciz "TalkpodA36Plus"
  .asciz " V0.0.1"
