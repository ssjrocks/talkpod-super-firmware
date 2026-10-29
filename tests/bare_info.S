  .syntax unified
  .cpu cortex-m4
  .thumb

/* Working serial firmware: uses bootloader's USART config at 115200.
   Reports RCU_CFG0, BRR, and other register values for clock analysis.
   Does NOT touch PA9, USART config, or clocks. */

.equ RCU_BASE,       0x40021000
.equ RCU_CTL,        RCU_BASE + 0x00
.equ RCU_CFG0,       RCU_BASE + 0x04
.equ RCU_AHBEN,      RCU_BASE + 0x14
.equ RCU_APB2EN,     RCU_BASE + 0x18
.equ RCU_CFG1,       RCU_BASE + 0x2C

.equ GPIOA_BASE,     0x48000000
.equ GPIOA_MODER,    GPIOA_BASE + 0x00
.equ GPIOA_OTYPER,   GPIOA_BASE + 0x04
.equ GPIOA_OSPEEDR,  GPIOA_BASE + 0x08
.equ GPIOA_PUPDR,    GPIOA_BASE + 0x0C
.equ GPIOA_IDR,      GPIOA_BASE + 0x10
.equ GPIOA_ODR,      GPIOA_BASE + 0x14
.equ GPIOA_AFRH,     GPIOA_BASE + 0x24

.equ GPIOB_BASE,     0x48000400
.equ GPIOB_MODER,    GPIOB_BASE + 0x00
.equ GPIOB_BSRR,     GPIOB_BASE + 0x18

.equ USART0_STAT,    0x40013800
.equ USART0_DATA,    0x40013804
.equ USART0_BRR,     0x40013808
.equ USART0_CTL0,    0x4001380C

.equ FWDGT_CTL,      0x40003000

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

  /* Enable GPIOB clock (don't touch GPIOA — bootloader has it) */
  ldr r0, =RCU_AHBEN
  ldr r1, [r0]
  orr r1, r1, #(1 << 18)
  str r1, [r0]
  movs r2, #16
1:  subs r2, r2, #1
  bne 1b

  /* PB7 output for backlight */
  ldr r0, =GPIOB_MODER
  ldr r1, [r0]
  bic r1, r1, #(3 << 14)
  orr r1, r1, #(1 << 14)
  str r1, [r0]

  /* Initial delay to let PC open serial port */
  ldr r0, =FWDGT_CTL
  movw r1, #0xAAAA
  str r1, [r0]
  ldr r0, =5000000
  subs r0, r0, #1
  bne .-2

  /* PB7 ON */
  ldr r0, =GPIOB_BSRR
  mov r1, #(1 << 7)
  str r1, [r0]

  /* ====== Send system info ====== */
  adr r4, msg_header
  bl print_str

  /* RCU_CTL */
  adr r4, lbl_rcu_ctl
  bl print_str
  ldr r0, =RCU_CTL
  ldr r6, [r0]
  bl print_hex32
  bl print_crlf

  /* RCU_CFG0 */
  adr r4, lbl_rcu_cfg0
  bl print_str
  ldr r0, =RCU_CFG0
  ldr r6, [r0]
  bl print_hex32
  bl print_crlf

  /* RCU_CFG1 */
  adr r4, lbl_rcu_cfg1
  bl print_str
  ldr r0, =RCU_CFG1
  ldr r6, [r0]
  bl print_hex32
  bl print_crlf

  /* RCU_AHBEN */
  adr r4, lbl_rcu_ahben
  bl print_str
  ldr r0, =RCU_AHBEN
  ldr r6, [r0]
  bl print_hex32
  bl print_crlf

  /* RCU_APB2EN */
  adr r4, lbl_rcu_apb2en
  bl print_str
  ldr r0, =RCU_APB2EN
  ldr r6, [r0]
  bl print_hex32
  bl print_crlf

  /* USART0_BRR */
  adr r4, lbl_brr
  bl print_str
  ldr r0, =USART0_BRR
  ldrh r6, [r0]
  bl print_hex16
  bl print_crlf

  /* USART0_CTL0 */
  adr r4, lbl_ctl0
  bl print_str
  ldr r0, =USART0_CTL0
  ldrh r6, [r0]
  bl print_hex16
  bl print_crlf

  /* GPIOA_MODER */
  adr r4, lbl_gpioa_moder
  bl print_str
  ldr r0, =GPIOA_MODER
  ldr r6, [r0]
  bl print_hex32
  bl print_crlf

  /* GPIOA_OTYPER */
  adr r4, lbl_gpioa_otyper
  bl print_str
  ldr r0, =GPIOA_OTYPER
  ldr r6, [r0]
  bl print_hex32
  bl print_crlf

  /* GPIOA_OSPEEDR */
  adr r4, lbl_gpioa_ospeedr
  bl print_str
  ldr r0, =GPIOA_OSPEEDR
  ldr r6, [r0]
  bl print_hex32
  bl print_crlf

  /* GPIOA_AFRH (has AF config for PA9/PA10) */
  adr r4, lbl_gpioa_afrh
  bl print_str
  ldr r0, =GPIOA_AFRH
  ldr r6, [r0]
  bl print_hex32
  bl print_crlf

  /* GPIOA_ODR */
  adr r4, lbl_gpioa_odr
  bl print_str
  ldr r0, =GPIOA_ODR
  ldrh r6, [r0]
  bl print_hex16
  bl print_crlf

  /* Separator */
  adr r4, msg_sep
  bl print_str

  /* ====== Main blink + heartbeat loop ====== */
main_loop:
  ldr r0, =FWDGT_CTL
  movw r1, #0xAAAA
  str r1, [r0]

  /* PB7 toggle OFF */
  ldr r0, =GPIOB_BSRR
  mov r1, #(1 << 23)
  str r1, [r0]

  /* Send full register dump each cycle */
  adr r4, msg_header
  bl print_str

  adr r4, lbl_rcu_ctl
  bl print_str
  ldr r0, =RCU_CTL
  ldr r6, [r0]
  bl print_hex32
  bl print_crlf

  adr r4, lbl_rcu_cfg0
  bl print_str
  ldr r0, =RCU_CFG0
  ldr r6, [r0]
  bl print_hex32
  bl print_crlf

  adr r4, lbl_brr
  bl print_str
  ldr r0, =USART0_BRR
  ldrh r6, [r0]
  bl print_hex16
  bl print_crlf

  adr r4, lbl_ctl0
  bl print_str
  ldr r0, =USART0_CTL0
  ldrh r6, [r0]
  bl print_hex16
  bl print_crlf

  adr r4, lbl_gpioa_moder
  bl print_str
  ldr r0, =GPIOA_MODER
  ldr r6, [r0]
  bl print_hex32
  bl print_crlf

  adr r4, lbl_gpioa_afrh
  bl print_str
  ldr r0, =GPIOA_AFRH
  ldr r6, [r0]
  bl print_hex32
  bl print_crlf

  adr r4, msg_sep
  bl print_str

  ldr r0, =FWDGT_CTL
  movw r1, #0xAAAA
  str r1, [r0]

  ldr r0, =10000000
2:  subs r0, r0, #1
  bne 2b

  /* PB7 toggle ON */
  ldr r0, =GPIOB_BSRR
  mov r1, #(1 << 7)
  str r1, [r0]

  ldr r0, =FWDGT_CTL
  movw r1, #0xAAAA
  str r1, [r0]

  ldr r0, =10000000
3:  subs r0, r0, #1
  bne 3b

  b main_loop
  .size reset_handler, . - reset_handler

  /* ====== UART helpers ====== */

  /* Print null-terminated string at r4 */
  .thumb_func
  .type print_str, %function
print_str:
  push {lr}
ps_loop:
  ldrb r6, [r4]
  cbz r6, ps_done
  adds r4, r4, #1
  bl uart_putc
  b ps_loop
ps_done:
  pop {pc}
  .size print_str, . - print_str

  /* Print r6 as 8-digit hex */
  .thumb_func
  .type print_hex32, %function
print_hex32:
  push {r6, lr}
  movs r5, #28
ph32:
  lsr r6, r6, r5
  and r6, r6, #0xF
  cmp r6, #10
  blt ph32_d
  add r6, r6, #('A' - 10)
  b ph32_s
ph32_d:
  add r6, r6, #'0'
ph32_s:
  bl uart_putc
  cbz r5, ph32_done
  subs r5, r5, #4
  ldr r6, [sp]          /* reload original r6 */
  b ph32
ph32_done:
  pop {r6, pc}
  .size print_hex32, . - print_hex32

  /* Print r6 as 4-digit hex */
  .thumb_func
  .type print_hex16, %function
print_hex16:
  push {r6, lr}
  movs r5, #12
ph16:
  lsr r6, r6, r5
  and r6, r6, #0xF
  cmp r6, #10
  blt ph16_d
  add r6, r6, #('A' - 10)
  b ph16_s
ph16_d:
  add r6, r6, #'0'
ph16_s:
  bl uart_putc
  cbz r5, ph16_done
  subs r5, r5, #4
  ldr r6, [sp]
  b ph16
ph16_done:
  pop {r6, pc}
  .size print_hex16, . - print_hex16

  /* Print CR LF */
  .thumb_func
  .type print_crlf, %function
print_crlf:
  push {lr}
  movs r6, #0x0D
  bl uart_putc
  movs r6, #0x0A
  bl uart_putc
  pop {pc}
  .size print_crlf, . - print_crlf

  /* Send char in r6 via USART0 */
  .thumb_func
  .type uart_putc, %function
uart_putc:
  ldr r0, =USART0_STAT
  ldr r3, =200000
txe_w:
  ldrh r1, [r0]
  tst r1, #(1 << 7)
  bne txe_ok
  subs r3, r3, #1
  bne txe_w
  bx lr
txe_ok:
  ldr r0, =USART0_DATA
  strh r6, [r0]
  bx lr
  .size uart_putc, . - uart_putc

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

  /* ====== String data ====== */
  .align 2
msg_header:
  .asciz "=== A36SE System Info ===\r\n"
lbl_rcu_ctl:
  .asciz "RCU_CTL:    "
lbl_rcu_cfg0:
  .asciz "RCU_CFG0:   "
lbl_rcu_cfg1:
  .asciz "RCU_CFG1:   "
lbl_rcu_ahben:
  .asciz "RCU_AHBEN:  "
lbl_rcu_apb2en:
  .asciz "RCU_APB2EN: "
lbl_brr:
  .asciz "USART0_BRR: "
lbl_ctl0:
  .asciz "USART0_CTL0:"
lbl_gpioa_moder:
  .asciz "GPIOA_MODER:"
lbl_gpioa_otyper:
  .asciz "GPIOA_OTYPER:"
lbl_gpioa_ospeedr:
  .asciz "GPIOA_OSPDR:"
lbl_gpioa_afrh:
  .asciz "GPIOA_AFRH: "
lbl_gpioa_odr:
  .asciz "GPIOA_ODR:  "
msg_sep:
  .asciz "---\r\n"
msg_beat:
  .asciz ".\r\n"

  .section .rodata.markers, "a", %progbits
  .asciz "PROGRAMJC37U"
  .asciz "TalkpodA36Plus"
  .asciz " V0.0.1"
