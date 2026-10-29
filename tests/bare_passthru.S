  .syntax unified
  .cpu cortex-m4
  .thumb

/* Minimal firmware: do NOT touch PA9/USART/clocks.
   Use whatever the bootloader left configured.
   Just enable GPIOB for backlight and send via USART0.

   Theory: the bootloader sets up PLL to 108 MHz, configures
   PA9 as USART0_TX (AF1), sets BRR=1875 (57600), enables USART.
   After deciding to run firmware (no programmer detected), it
   jumps to us. We inherit all that config.

   Test: listen at 57600 on PC. If we see "ALIVE", the bootloader
   left USART ready. If not, we try re-enabling USART ourselves. */

.equ RCU_AHBEN,     0x40021014
.equ GPIOB_BASE,    0x48000400
.equ GPIOB_MODER,   GPIOB_BASE + 0x00
.equ GPIOB_BSRR,    GPIOB_BASE + 0x18
.equ USART0_STAT,   0x40013800
.equ USART0_DATA,   0x40013804
.equ USART0_BRR,    0x40013808
.equ USART0_CTL0,   0x4001380C
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
  /* Feed watchdog */
  ldr r0, =FWDGT_CTL
  movw r1, #0xAAAA
  str r1, [r0]

  /* Enable GPIOB clock only (keep everything else as bootloader left it) */
  ldr r0, =RCU_AHBEN
  ldr r1, [r0]
  orr r1, r1, #(1 << 18)     /* GPIOB */
  str r1, [r0]
  movs r2, #16
1:  subs r2, r2, #1
  bne 1b

  /* PB7 output for backlight (proof we're running) */
  ldr r0, =GPIOB_MODER
  ldr r1, [r0]
  bic r1, r1, #(3 << 14)
  orr r1, r1, #(1 << 14)
  str r1, [r0]
  ldr r0, =GPIOB_BSRR
  mov r1, #(1 << 7)
  str r1, [r0]

  /* ====== PHASE 1: Try sending with bootloader's USART config ====== */
  /* Check if USART is already enabled by reading CTL0 UEN bit (bit 13) */
  ldr r0, =USART0_CTL0
  ldrh r1, [r0]
  tst r1, #(1 << 13)
  beq phase2          /* USART not enabled, skip to phase 2 */

  /* USART is enabled! Try sending "1:" prefix then ALIVE */
  movs r6, #'1'
  bl uart_send_char
  movs r6, #':'
  bl uart_send_char
  adr r4, msg_alive
  movs r5, #7
p1_msg:
  ldrb r6, [r4]
  adds r4, r4, #1
  bl uart_send_char
  subs r5, r5, #1
  bne p1_msg

  /* Wait for TC */
  bl uart_wait_tc

  /* Short delay before phase 2 */
  ldr r0, =2000000
  subs r0, r0, #1
  bne .-2

  /* ====== PHASE 2: Re-enable USART with bootloader's BRR ====== */
phase2:
  ldr r0, =FWDGT_CTL
  movw r1, #0xAAAA
  str r1, [r0]

  /* Disable USART, set BRR, re-enable */
  ldr r0, =USART0_CTL0
  movs r1, #0
  strh r1, [r0]

  /* Don't change BRR — keep whatever bootloader set.
     But just in case it's cleared, read it first */
  ldr r0, =USART0_BRR
  ldrh r7, [r0]          /* r7 = current BRR */

  /* If BRR is 0 or unreasonable, set it for 57600 @ 108MHz */
  cmp r7, #10
  blt set_default_brr
  ldr r1, =20000
  cmp r7, r1
  bgt set_default_brr
  b brr_ok

set_default_brr:
  movw r7, #1875          /* 108MHz / 57600 */
  ldr r0, =USART0_BRR
  strh r7, [r0]

brr_ok:
  /* Enable USART: UEN + TE */
  ldr r0, =USART0_CTL0
  movw r1, #0x2008        /* UEN=bit13, TE=bit3 */
  strh r1, [r0]
  movs r2, #64
2:  subs r2, r2, #1
  bne 2b

  /* Send "2:" prefix then BRR value as hex, then ALIVE */
  movs r6, #'2'
  bl uart_send_char
  movs r6, #':'
  bl uart_send_char

  /* Send BRR as 4 hex chars */
  mov r8, r7
  movs r10, #12           /* shift: start at bit 12 */
brr_hex:
  lsr r6, r8, r10
  and r6, r6, #0xF
  cmp r6, #10
  blt brr_digit
  add r6, r6, #('A' - 10)
  b brr_send
brr_digit:
  add r6, r6, #'0'
brr_send:
  bl uart_send_char
  subs r10, r10, #4
  bpl brr_hex

  movs r6, #' '
  bl uart_send_char

  adr r4, msg_alive
  movs r5, #7
p2_msg:
  ldrb r6, [r4]
  adds r4, r4, #1
  bl uart_send_char
  subs r5, r5, #1
  bne p2_msg
  bl uart_wait_tc

  /* Short delay */
  ldr r0, =2000000
  subs r0, r0, #1
  bne .-2

  /* ====== PHASE 3: Try 8 MHz assumption (BRR=139 for 57600) ====== */
phase3:
  ldr r0, =FWDGT_CTL
  movw r1, #0xAAAA
  str r1, [r0]

  ldr r0, =USART0_CTL0
  movs r1, #0
  strh r1, [r0]

  ldr r0, =USART0_BRR
  movw r1, #139            /* 8MHz / 57600 ≈ 139 */
  strh r1, [r0]

  ldr r0, =USART0_CTL0
  movw r1, #0x2008
  strh r1, [r0]
  movs r2, #64
3:  subs r2, r2, #1
  bne 3b

  movs r6, #'3'
  bl uart_send_char
  movs r6, #':'
  bl uart_send_char
  adr r4, msg_alive
  movs r5, #7
p3_msg:
  ldrb r6, [r4]
  adds r4, r4, #1
  bl uart_send_char
  subs r5, r5, #1
  bne p3_msg
  bl uart_wait_tc

  ldr r0, =2000000
  subs r0, r0, #1
  bne .-2

  /* ====== PHASE 4: Try 8 MHz assumption (BRR=833 for 9600) ====== */
phase4:
  ldr r0, =FWDGT_CTL
  movw r1, #0xAAAA
  str r1, [r0]

  ldr r0, =USART0_CTL0
  movs r1, #0
  strh r1, [r0]

  ldr r0, =USART0_BRR
  movw r1, #833            /* 8MHz / 9600 ≈ 833 */
  strh r1, [r0]

  ldr r0, =USART0_CTL0
  movw r1, #0x2008
  strh r1, [r0]
  movs r2, #64
4:  subs r2, r2, #1
  bne 4b

  movs r6, #'4'
  bl uart_send_char
  movs r6, #':'
  bl uart_send_char
  adr r4, msg_alive
  movs r5, #7
p4_msg:
  ldrb r6, [r4]
  adds r4, r4, #1
  bl uart_send_char
  subs r5, r5, #1
  bne p4_msg
  bl uart_wait_tc

  /* ====== MAIN BLINK LOOP (phase 2 repeats at 57600) ====== */
main_loop:
  ldr r0, =FWDGT_CTL
  movw r1, #0xAAAA
  str r1, [r0]

  /* PB7 OFF */
  ldr r0, =GPIOB_BSRR
  mov r1, #(1 << 23)
  str r1, [r0]

  /* Reconfigure USART at bootloader's BRR (saved in r7 from phase 2) */
  ldr r0, =USART0_CTL0
  movs r1, #0
  strh r1, [r0]
  ldr r0, =USART0_BRR
  strh r7, [r0]
  ldr r0, =USART0_CTL0
  movw r1, #0x2008
  strh r1, [r0]
  movs r2, #32
5:  subs r2, r2, #1
  bne 5b

  /* Send repeated "ALIVE\r\n" */
  adr r4, msg_alive
  movs r5, #7
ml_msg:
  ldrb r6, [r4]
  adds r4, r4, #1
  bl uart_send_char
  subs r5, r5, #1
  bne ml_msg
  bl uart_wait_tc

  ldr r0, =5000000
6:  subs r0, r0, #1
  bne 6b

  /* PB7 ON */
  ldr r0, =GPIOB_BSRR
  mov r1, #(1 << 7)
  str r1, [r0]

  ldr r0, =5000000
7:  subs r0, r0, #1
  bne 7b

  b main_loop
  .size reset_handler, . - reset_handler

  /* Send char in r6 via USART0, with timeout */
  .thumb_func
  .type uart_send_char, %function
uart_send_char:
  push {lr}
  ldr r0, =USART0_STAT
  ldr r3, =200000
txe_w:
  ldrh r1, [r0]
  tst r1, #(1 << 7)
  bne txe_ok
  subs r3, r3, #1
  bne txe_w
  pop {pc}
txe_ok:
  ldr r0, =USART0_DATA
  strh r6, [r0]
  pop {pc}
  .size uart_send_char, . - uart_send_char

  .thumb_func
  .type uart_wait_tc, %function
uart_wait_tc:
  ldr r0, =USART0_STAT
  ldr r3, =400000
tc_w:
  ldrh r1, [r0]
  tst r1, #(1 << 6)
  bne tc_done
  subs r3, r3, #1
  bne tc_w
tc_done:
  bx lr
  .size uart_wait_tc, . - uart_wait_tc

  .thumb_func
  .type hardfault_handler, %function
hardfault_handler:
  b hardfault_handler
  .size hardfault_handler, . - hardfault_handler

  .thumb_func
  .type hang, %function
hang:
  b hang
  .size hang, . - hang

  .align 2
msg_alive:
  .ascii "ALIVE\r\n"

  .section .rodata.markers, "a", %progbits
  .asciz "PROGRAMJC37U"
  .asciz "TalkpodA36Plus"
  .asciz " V0.0.1"
