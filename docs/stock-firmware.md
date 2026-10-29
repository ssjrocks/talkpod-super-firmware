# Stock firmware notes (A36Plus-EN-V1.22-Super)

What the stock firmware does to the hardware, found with `tools/re/call_stock.py` (calls one stock
function in Unicorn and logs BK4819 register and GPIO activity) and static disassembly. Addresses are
for the decrypted V1.22 Super image loaded at 0x08000400.

## MCU

Artery **AT32F421C8T7** (DBG IDCODE 0x50020100), Cortex-M4 without FPU, 64 KB flash, 16 KB RAM. The
bootloader leaves the PLL at 48 MHz; stock firmware runs at 108 MHz (its USART BRR is 1875 for 57600).
The USART has the older register layout (STAT +0, DATA +4, BAUD +8, CTL0 +0xC). Stock runs on an RTOS
(SVC / PendSV / SysTick handlers), so the whole image can't be run in Unicorn; single functions can.

## Board revision tag

Stock compares 7 bytes at **0x08000300** (inside the bootloader) with `HWVER02`. Match: flag
0x20000115 = 1 and extra setup (0x8006404, TMR3 PWM). No match: flag = 0. The RF switch pins differ
between the two (below).

## Pins (stock GPIO init 0x80060ac, final state)

| Pin | Use |
|---|---|
| PA0 | input (unknown) |
| PA1 | battery voltage (ADC) |
| PA2, PA3, PC13 | BK4819 SCK, SDA (bidirectional), CS. PA3 is also BK1080 SDA |
| PA4-PA7 | W25Q16 SPI flash: CS, SCK, MISO, MOSI (SPI0, AF0) |
| PA8 | output; set 10 ms after the BK1080 powers up (FM broadcast), cleared when leaving it |
| PA9, PA10 | USART0 TX, RX. PA10 is also PTT (low = pressed) |
| PA11 | output; set when a voice prompt starts playing (from SPI flash via TMR1 + DMA) |
| PA12 | output (Jamie793: MIC_EN) |
| PA13, PA14 | LEDs, driven by 0x8006a30 / 0x8006a04 (0 off, 1 on, 2 toggle). PA14 turns on when a signal opens the squelch (green) |
| PA15 | **power hold**: cleared (then 500 ms, then reset) to switch the radio off |
| PF7 | **power switch sense**, input with pull-up: high for 50 ms means switched off |
| PF6 | BK1080 SCL |
| PB0, PB1, PC14, PC15 | RF path switches (below) |
| PB2, PB7, PB10, PB12, PB13, PB15 | LCD CS, backlight, DC, RST, SCK, MOSI |
| PB3-PB6 / PB8, PB9, PB11, PB14 | key rows / columns (columns with pull-ups) |

No MCU pin is switched when received audio is unmuted (0x8008ac0: LED, REG_47, RF path only); the
speaker amplifier enable is not identified yet.

## Band index (0x8001ed4)

`f100k = freq_10Hz / 10000`. Band 0 if f100k < 3000 (below 300 MHz), else 1; band 4 for
108.0-136.0 MHz (airband). Stock RX range check: 18-660 MHz and 840-1000 MHz.

## RF path switch 0x8008710(band, mode) (mode 0 off, 1 RX, 2 TX)

Off (both revisions): BK4819 REG_33 = 0x0000, clear PC15, PC14, PB0, PB1.

| | flag 0 (not HWVER02) | flag 1 (HWVER02) |
|---|---|---|
| RX, even band (0, 2, 4) | clear PC14, set PB1 | clear PC14, set PB0, TMR3 C4DT = 0xFF |
| RX, odd band (1, 3) | clear PB1, set PC14 | clear PB0, set PC14 |
| TX, even band (0, 2) | clear PB0, set PC15 | BK GPIO2 = 0, set PC15 |
| TX, odd band (1, 3) | clear PC15, set PB0 | clear PC15, BK GPIO2 = 1 |
| TX, band 4 | clear PB0, PC15 (no TX path) | BK GPIO2 = 0, clear PC15, TMR3 C4DT = 0 |

## BK4819

Bit-banged: `bk4819_write_reg` 0x8009c08(reg, value), `read_reg` 0x8009800(reg) (reg | 0x80, SDA
switched to input, bit read after each SCK rising edge). BK GPIO via REG_33: 0x80096a0(mask, value).

Init 0x800842c: 00=8000, 00=0000, 37=1D0F, 13=03BE, 12=037B, 11=027B, 10=007A, 14=0019, 49=2A38,
7B=8420, 7D=E952, 48=B3FF, 1E=4C58, 1F=A656, 3E=A037, 3F=07FE, 2A=7FFF, 28=6B00, 53=E678, 2C=5705,
4B=7102, 40=(read & F000) | 04D2, 77=88EF, 26=13A0, 4E=6F15, 4F=3F3E, 09=006F 106B 2067 3062 4050 5047
603A 702C 8041 9037 A025 B017 C0E4 D0CB E0B5 F09F, 74=FA02, 44=8F88, 45=3201.

REG_30 by 0x800989c(mode) (written 0 or 0x0200 first): 0 = 0x0000 off, 1 = 0xBFF1 RX,
2 = 0xC1FE TX, 3 = 0x0302 local tone/beep, 4 = 0xC3FA TX of a generated tone (no mic).

RX start 0x80097d0 (channel without tones): 37=1F0F, 30=0000, 38/39 = freq in 10 Hz (low/high
word), 43 = 3028 wide / 4048 narrow, 78 = squelch thresholds (from calibration tables in RAM at
0x200000b4, per band), 4F=0000, 51=9000, 07=0471, 07=21CD, 52=0292, 30=0200, 30=BFF1.

TX start 0x8009b70: 37=1D0F, REG_30 off, set freq, 0x80097ba (REG_30 tweak), 0x80014d4 (REG_51
tones), 0x80093a0 (REG_7D mic gain), REG_30 mode 2, 0x800961c.

Audio output 0x8009a40(type): REG_47 = 0x6040 | table[type] << ... giving 6040 mute, 6140 FM,
6340 beep; FM becomes 6740 (**AM**) when the channel modulation byte (0x200000a4) is 6.
REG_48 = 0xB00F | (gain & 0x3F) << 4 (init uses 0xB3FF).

Other: RSSI read REG_67 (0x80086b0), status REG_0C (0x80086c2 ...), frequency scan result REG_0D/0E
(0x8009434), CTCSS/DCS scan results REG_68/69/6A (0x800955c), squelch REG_78/4F (0x80098f4), TX
power REG_36 (0x8009aa4), FSK modem setup REG_58/59/5A-5F/70/72/3F (0x80094aa, 0x80096c8, 0x8009512).
