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
| PA0 | analog input, ADC channel 0 (stock sets its sample time with channel 1's). Not received audio: sampled at 85 kHz it is a steady level, about 163 of 4095 idle and 28 with a local 1 kHz tone (2026-10-30), so it looks like a filtered audio level (VOX or similar). No raw audio reaches the MCU. |
| PA1 | battery voltage (ADC) |
| PA2, PA3, PC13 | BK4819 SCK, SDA (bidirectional), CS. PA3 is also BK1080 SDA |
| PA4-PA7 | W25Q16 SPI flash: CS, SCK, MISO, MOSI (SPI0, AF0) |
| PA8 | output; set 10 ms after the BK1080 powers up (FM broadcast), cleared when leaving it |
| PA9, PA10 | USART0 TX, RX. PA10 is also PTT (low = pressed) |
| PA11 | output; set when a voice prompt starts playing (from SPI flash via TMR1 + DMA) |
| PA12 | **speaker amplifier enable**, high = on (confirmed on the radio). Stock 0x800b6e0(n) keeps a flag per audio source (odd n on, even n off) and drives PA12 high while any is set; Jamie793 called it MIC_EN |
| PA13, PA14 | LEDs, driven by 0x8006a30 / 0x8006a04 (0 off, 1 on, 2 toggle). PA14 turns on when a signal opens the squelch (green) |
| PA15 | output, left low by the bootloader; stock never sets it, only clears it before its power-off reset. Not a power hold (driving it high changed nothing useful); role unknown |
| PF7 | power switch sense, input with pull-up: low while on, high for 50 ms means switched off (the knob also cuts power itself) |
| PF6 | BK1080 SCL |
| PB0, PB1, PC14, PC15 | RF path switches (below) |
| PB2, PB7, PB10, PB12, PB13, PB15 | LCD CS, backlight, DC, RST, SCK, MOSI |
| PB3-PB6 / PB8, PB9, PB11, PB14 | key rows / columns (columns with pull-ups) |

Received audio is unmuted by 0x8008ac0: green LED, REG_47 = FM, speaker on (0x800b6e0(5): PA12),
RF path. **With the programming cable plugged into the speaker/mic jack the internal speaker is
cut off**: test audio with the cable unplugged.

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

RX on (0x8004980): speaker off, REG_47 mute, LED off, 30=0000, **28=6B38** (RX audio expander;
init leaves 6B00), 30=0200, 30=BFF1, RF path.

RX start 0x80097d0 (channel without tones): 37=1F0F, 30=0000, 38/39 = freq in 10 Hz (low/high
word), 43 = 3028 wide / 4048 narrow, 78 = squelch thresholds (from calibration tables in RAM at
0x200000b4, per band), 4F=0000, 51=9000, 07=0471, 07=21CD, 52=0292, 30=0200, 30=BFF1.

TX start 0x8009b70: 37=1D0F, REG_30 off, set freq, 0x80097ba (REG_30 tweak), 0x80014d4 (REG_51
tones), 0x80093a0 (REG_7D mic gain), REG_30 mode 2, 0x800961c.

Audio output 0x8009a40(type): REG_47 = 0x6040 | table[type] << ... giving 6040 mute, 6140 FM,
6340 beep; FM becomes 6740 (**AM**) when the channel modulation byte (0x200000a4) is 6.
REG_48 = 0xB00F | (gain & 0x3F) << 4 (init uses 0xB3FF).

Sleep/standby 0x800bda4: BK4819 init, RF path off, 30=0000, 37=1D00 (BK4819 powered down), RTC
wake-up set up, MCU into standby.

Other: RSSI read REG_67 (0x80086b0), status REG_0C (0x80086c2 ...), frequency scan result REG_0D/0E
(0x8009434), CTCSS/DCS scan results REG_68/69/6A (0x800955c), squelch REG_78/4F (0x80098f4), TX
power REG_36 (0x8009aa4), FSK modem setup REG_58/59/5A-5F/70/72/3F (0x80094aa, 0x80096c8, 0x8009512).

## External flash (SPI0, W25Q16-compatible; this radio: Zbit ZB25VQ16, ID 5E 60 15)

| Address | Contents |
|---|---|
| 0x0000-0x3FFF | 512 channels x 32 bytes (CHIRP mml_jc8810 layout: lbcd rx[4], tx[4] in 10 Hz units; flags byte 15: bit 6 narrow, bit 2 scan; name at 20, 12 chars) |
| 0x9000 | settings; squelch level 0-9 in the low nibble of 0x9000 |
| 0xF000-0xF0B0 | per-radio tables loaded to RAM 0x20000014-0x20000094 (TX power, not decoded yet) |
| 0xF0C0 / 0xF0D0 | squelch RSSI open / close per level 0-9 (raw: dBm = raw / 2 - 160) |
| 0xF0E0 | squelch correction, 400 MHz up, per 10 MHz (index clamped at 12) |
| 0xF0F0 | correction, 130-200 MHz, per 5 MHz (clamped at 9) |
| 0xF100 | correction, 200-300 MHz, per 5 MHz (clamped at 12) |
| 0xF110 | correction, 300-400 MHz, per 5 MHz from 350 (index 0 below 350) |
| 0xF210 | noise: [5 + band] threshold (band: 0 = 400 up, 1 = 130-200, 2 = 200-300, 3 = 300-400), [9] hysteresis |
| 0xF230 | band table, 5 bytes each: enable, low MHz, high MHz (BCD): 136-180, 400-520, 200-260, 350-400 |

Squelch (0x80098f4): REG_78 = (open[L] - corr) << 8 | (close[L] - corr), REG_4F = n << 8 | (n - hyst)
with n = noise[5 + band] & 0x7F. REG_0C bit 1 is the chip's squelch-open decision (0x80086c2);
bits 10, 11, 14, 15 are CTCSS/DCS results (0x80095ec, 0x8009b40).

Search (0x800af20 / 0x800af46): speaker off, REG_3F/59/70/58 = 0/2028/0/0, REG_32 = 8245; a result
when REG_0D bit 15 is clear: (REG_0D & 7FF) << 16 | REG_0E; REG_32 = 8244 stops. Two results within
250 Hz are averaged; results outside the selected band are discarded.

## Squelch timing (measured on the radio, v0.82 shell 'm')

After a retune onto a strong signal, RSSI (REG_67) and the noise indicator (REG_65 & 7F) settle
within 10-30 ms (noise 10-32 on a signal, 53-69 without), but REG_0C bit 1 rises only after
~40 ms + 5 ms per step of the squelch open delay, REG_4E bits 13:11 (stock 0x6F15: delay 5, 65 ms;
delay 2: 50 ms; delay 1: 45 ms). No false opens were seen at delays 0-5 without a signal. This
radio's RSSI reads about -95 to -125 dBm with no signal, above the calibrated open threshold
(-120 dBm at level 1, 476 MHz), so the noise criterion is what keeps the squelch closed.

## Squelch survey (v0.84 shell 'v', 470-490 MHz, 12.5 kHz steps, no signal)

Noise indicator: narrow 70-80 almost everywhere; wide 50-79, about a third of all frequencies at
50-64, scattered (not in blocks). With stock thresholds (open below 55) the wide squelch opened on
static and stopped scans. A signal reads noise 8-32. The 480.000 MHz spur (10th harmonic of the
48 MHz MCU clock this firmware runs at; RSSI -65..-80 dBm) opens the squelch in both modes.
The firmware now lowers the wide noise threshold by 15 and tightens noise by 3 and RSSI by 3 dB per
level above 1 (narrow level 1 = stock): wide level 1 then had 0 false opens in 900 steps, and a
strong signal opened levels 1 and 9 in both bandwidths after 65 ms.

## Flash usage (2 MB backup of 2026-10-30, after the radio's use with stock firmware)

Per 64 KB block, share of bytes not erased: 0x000000 19%, 0x010000 44%, 0x020000-0x08FFFF 97-99%
(splash, icons, fonts), 0x090000 47%, 0x09E000-0x10FFFF erased, 0x110000-0x18FFFF 98-99% (voice
prompts; index at 0x10FF80), 0x190000-0x191FFF used, 0x192000-0x1EFFFF erased, 0x1F0000-0x1F1FFF
used, rest erased. Stock can erase 4 KB / 32 KB / 64 KB / the whole chip (0x800b88c, 0x800b780,
0x800b7de, 0x800b844) and program pages (0x800ba24); its addresses are computed at run time.

This firmware writes only to 0x110000-0x1EFFFF (drivers/spiflash.h refuses anything else):
0x110000 sound pack, 0x120000-0x1DFFFF boot splash, 0x1E0000 settings (32-byte records, newest
valid wins). 0x110000-0x191FFF was the stock voice area (see Voice prompts).
The backup is in backups/ (not in git) and can be restored with a stock-firmware tool or a future
restore command.

## Voice prompts

Index at flash 0x110000: (u32 length, u32 offset) pairs, audio at offset + 0x90000, 143 prompts
(stock numbers 16-158; Super 1.22's table base 0x10FF80 counts from 16): 16-77 Mandarin, 78-148
English ("0".."100", "welcome", menu item names, "high", "low", "11".."19"), 149-158 beeps.
Factory data is standard IMA ADPCM (89 steps, 16-bit predictor), **high nibble first**; Super
1.22's decoder (0x800d4f0) clamps the index to 58 and the predictor to 12 bits, which clips this
data. Played as PWM on PA11 (TMR1 channel 4, AF2): Super uses period 2201 at 108 MHz with a
repetition count of 5 and DMA from the update event into C4DT, 8178 samples/s.
tools/extract_voice.py writes them as WAV files.

On 2026-10-30 the voice area 0x110000-0x191FFF was erased at the user's request and replaced by
this firmware's sound pack (tools/soundpack.py: ten sounds, 27.9 KB). All prompts are in the
2 MB backup; stock firmware will have no voice prompts unless the backup is restored.
