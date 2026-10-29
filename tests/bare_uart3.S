  .syntax unified
  .cpu cortex-m4
  .thumb

/* GPIO: F330 layout (AHB bus, MODER registers) */
.equ RCU_BASE,      0x40021000
.equ RCU_AHBEN,     RCU_BASE + 0x14
.equ RCU_APB2EN,    RCU_BASE + 0x18

.equ GPIOA_BASE,    0x48000000
.equ GPIOA_MODER,   GPIOA_BASE + 0x00
.equ GPIOA_AFRH,    GPIOA_BASE + 0x24
.equ GPIOB_BASE,    0x48000400
.equ GPIOB_MODER,   GPIOB_BASE + 0x00
.equ GPIOB_BSRR,    GPIOB_BASE + 0x18

/* USART0: F303 layout (STAT=+0, DATA=+4, BRR=+8, CTL0=+C) */
.equ USART0_BASE,   0x40013800
.equ USART0_STAT,   USART0_BASE + 0x00
.equ USART0_DATA,   USART0_BASE + 0x04
.equ USART0_BRR,    USART0_BASE + 0x08
.equ USART0_CTL0,   USART0_BASE + 0x0C

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

  /* Enable USART0 on APB2 */
  ldr r0, =RCU_APB2EN
  ldr r1, [r0]
  orr r1, r1, #(1 << 14)
  str r1, [r0]

  movs r2, #64
1:  subs r2, r2, #1
  bne 1b

  /* PB7 output, set HIGH (backlight) */
  ldr r0, =GPIOB_MODER
  ldr r1, [r0]
  bic r1, r1, #(3 << 14)
  orr r1, r1, #(1 << 14)
  str r1, [r0]
  ldr r0, =GPIOB_BSRR
  mov r1, #(1 << 7)
  str r1, [r0]

  /* PA9 as AF for USART0 TX */
  ldr r0, =GPIOA_MODER
  ldr r1, [r0]
  bic r1, r1, #(3 << 18)
  orr r1, r1, #(2 << 18)
  str r1, [r0]
  ldr r0, =GPIOA_AFRH
  ldr r1, [r0]
  bic r1, r1, #(0xF << 4)
  orr r1, r1, #(1 << 4)
  str r1, [r0]

  /* BRR for 108 MHz: 108000000/115200 = 937.5 → 938 */
  ldr r0, =USART0_BRR
  movw r1, #938
  str r1, [r0]

  /* Enable USART: UEN (bit 13 on F303) + TE (bit 3) */
  ldr r0, =USART0_CTL0
  movw r1, #0x200C        /* UEN=bit13, TE=bit3, RE=bit2 */
  str r1, [r0]

  movs r2, #32
2:  subs r2, r2, #1
  bne 2b

  /* === Main blink + serial loop === */
send_loop:
  ldr r0, =FWDGT_CTL
  movw r1, #0xAAAA
  str r1, [r0]

  /* PB7 ON */
  ldr r0, =GPIOB_BSRR
  mov r1, #(1 << 7)
  str r1, [r0]

  /* Send "ALIVE\r\n" with timeout per char */
  adr r4, msg
  movs r5, #7
send_char:
  ldrb r6, [r4]
  adds r4, r4, #1

  /* Wait for TXE (bit 7) with timeout */
  ldr r7, =USART0_STAT
  ldr r3, =50000          /* timeout counter */
txe_wait:
  ldr r0, [r7]
  tst r0, #(1 << 7)
  bne txe_ready
  subs r3, r3, #1
  bne txe_wait
  b skip_char             /* timed out — don't hang */
txe_ready:
  ldr r0, =USART0_DATA
  str r6, [r0]
skip_char:
  subs r5, r5, #1
  bne send_char

  /* Delay ~556ms at 108 MHz */
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
  /* Rapid toggle PA9 as distress signal */
  ldr r0, =GPIOA_MODER
  ldr r1, [r0]
  bic r1, r1, #(3 << 18)
  orr r1, r1, #(1 << 18)
  str r1, [r0]
hf_loop:
  ldr r0, =0x48000018     /* GPIOA BSRR */
  mov r1, #(1 << 9)
  str r1, [r0]
  mov r1, #(1 << 25)
  str r1, [r0]
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
