  .syntax unified
  .cpu cortex-m4
  .thumb

/* GD32F330/F3x0 registers */
.equ RCU_BASE,      0x40021000
.equ RCU_AHBEN,     RCU_BASE + 0x14
.equ RCU_APB2EN,    RCU_BASE + 0x18

.equ GPIOA_BASE,    0x48000000
.equ GPIOA_MODER,   GPIOA_BASE + 0x00
.equ GPIOA_AFRH,    GPIOA_BASE + 0x24
.equ GPIOA_BSRR,    GPIOA_BASE + 0x18

.equ GPIOB_BASE,    0x48000400
.equ GPIOB_MODER,   GPIOB_BASE + 0x00
.equ GPIOB_BSRR,    GPIOB_BASE + 0x18

.equ USART0_BASE,   0x40013800
.equ USART0_CTL0,   USART0_BASE + 0x00
.equ USART0_BRR,    USART0_BASE + 0x0C
.equ USART0_STAT,   USART0_BASE + 0x1C
.equ USART0_TDATA,  USART0_BASE + 0x28

.equ FWDGT_CTL,     0x40003000

  .section .isr_vector, "a", %progbits
  .type vectors, %object
vectors:
  .word 0x20003E48        /* stock SP */
  .word reset_handler
  .word hang
  .word hardfault_handler
  .word hang              /* MemManage */
  .word hang              /* BusFault */
  .word hang              /* UsageFault */
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

  /* Enable GPIOA + GPIOB clocks on AHB */
  ldr r0, =RCU_AHBEN
  ldr r1, [r0]
  orr r1, r1, #(1 << 17)   /* GPIOAEN */
  orr r1, r1, #(1 << 18)   /* GPIOBEN */
  str r1, [r0]

  /* Enable USART0 clock on APB2 */
  ldr r0, =RCU_APB2EN
  ldr r1, [r0]
  orr r1, r1, #(1 << 14)   /* USART0EN */
  str r1, [r0]

  /* Stabilization delay */
  movs r2, #64
1:  subs r2, r2, #1
  bne 1b

  /* Configure PB7 as output (backlight) */
  ldr r0, =GPIOB_MODER
  ldr r1, [r0]
  bic r1, r1, #(3 << 14)
  orr r1, r1, #(1 << 14)   /* 01 = output */
  str r1, [r0]

  /* Set PB7 HIGH (backlight on) */
  ldr r0, =GPIOB_BSRR
  mov r1, #(1 << 7)
  str r1, [r0]

  /* Configure PA9 as AF (USART0 TX) */
  ldr r0, =GPIOA_MODER
  ldr r1, [r0]
  bic r1, r1, #(3 << 18)
  orr r1, r1, #(2 << 18)   /* 10 = alternate function */
  str r1, [r0]

  /* PA9 AF1 = USART0. AFRH bits[7:4] = 0001 for pin 9 */
  ldr r0, =GPIOA_AFRH
  ldr r1, [r0]
  bic r1, r1, #(0xF << 4)
  orr r1, r1, #(1 << 4)    /* AF1 = USART0 */
  str r1, [r0]

  /* Configure USART0: 115200 baud at 8 MHz IRC */
  ldr r0, =USART0_BRR
  movs r1, #69
  str r1, [r0]

  /* Enable USART: UEN (bit 0) + TEN (bit 3) */
  ldr r0, =USART0_CTL0
  movs r1, #0x09
  str r1, [r0]

  /* Brief delay for USART to stabilize */
  movs r2, #32
2:  subs r2, r2, #1
  bne 2b

send_loop:
  /* Feed watchdog */
  ldr r0, =FWDGT_CTL
  movw r1, #0xAAAA
  str r1, [r0]

  /* Toggle PB7 */
  ldr r0, =GPIOB_BSRR
  mov r1, #(1 << 7)
  str r1, [r0]

  adr r4, msg
  movs r5, #7
send_char:
  ldrb r6, [r4]
  adds r4, r4, #1
  ldr r7, =USART0_STAT
3:  ldr r0, [r7]
  tst r0, #(1 << 7)
  beq 3b
  ldr r0, =USART0_TDATA
  str r6, [r0]
  subs r5, r5, #1
  bne send_char

  /* Delay ~500ms at 8 MHz */
  ldr r0, =400000
4:  subs r0, r0, #1
  bne 4b

  /* PB7 off */
  ldr r0, =GPIOB_BSRR
  mov r1, #(1 << 23)
  str r1, [r0]

  ldr r0, =400000
5:  subs r0, r0, #1
  bne 5b

  b send_loop
  .size reset_handler, . - reset_handler

  .thumb_func
  .type hardfault_handler, %function
hardfault_handler:
  ldr r0, =GPIOA_MODER
  ldr r1, [r0]
  bic r1, r1, #(3 << 18)
  orr r1, r1, #(1 << 18)
  str r1, [r0]
hf_loop:
  ldr r0, =GPIOA_BSRR
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
