# The Talkpod A36SE: what we know about the hardware

Everything here was measured on a real radio (board tag HWVER02, stock firmware
A36Plus-EN-V1.22-Super), found in the stock firmware with `tools/re/call_stock.py` and static
disassembly ([stock-firmware.md](stock-firmware.md) has the addresses), or proven by this
firmware working. Where something is a guess, it says so. Older notes that circulate elsewhere
(a GD32F303 with 256 KB, a 161 x 128 display, PB0 as BK1080 clock) are wrong.

## At a glance

| Part | What it is |
|---|---|
| MCU | Artery **AT32F421C8T7**: Cortex-M4 without FPU, 64 KB flash, 16 KB RAM, LQFP-48 |
| Transceiver | Beken **BK4819** (FM/AM RX, FM TX, FSK modem, tone generator, frequency counter) |
| Broadcast FM | Beken BK1080 (stock FM radio; unused by this firmware) |
| External flash | 2 MB SPI NOR, W25Q16-compatible; this radio: Zbit ZB25VQ16, JEDEC ID 5E 60 15 |
| Display | ST7735S, 128 x 128 visible, RGB565, on SPI |
| Keys | 4 x 4 matrix, two side keys, a top key, PTT |
| Audio out | speaker amplifier enabled by PA12; BK4819 AF output, or MCU PWM for prompts |
| Serial | USART0 on the speaker/mic jack (K-plug programming cable) |

## MCU

The chip identifies itself as an AT32F421C8T7: DBG IDCODE at 0xE0042000 reads 0x50020100, and the
flash size register 0x1FFFF7E0 reads 64 KB.

- **Flash:** 64 KB. The first 1 KB (0x08000000–0x080003FF) is the bootloader; applications start
  at **0x08000400** and may be up to 63 KB.
- **RAM:** 16 KB at 0x20000000. The bootloader only starts an image whose initial stack pointer
  passes its check. Both stock and working test images use **0x20003E48**; 0x20004000 never
  started. This firmware uses 0x20003E48 and leaves the top 440 bytes alone.
- **Peripheral compatibility:**
  - Clocks (RCU), GPIO (on AHB at 0x48000000), SPI and the ADC match GigaDevice's GD32F3x0 at
    register level, so the vendored GD32F3x0 library works for those.
  - The **USART does not match:** it has the older layout (STAT +0x00, DATA +0x04, BAUD +0x08,
    CTL0 +0x0C). `src/debug.c` drives it directly.
  - TMR1 is GD's TIMER0 (IRQ 13, `TIMER0_BRK_UP_TRG_COM_IRQHandler`, APB2EN bit 11).
- **DWT cycle counter:** works. The APRS transmitter uses it for bit timing.

### Clocks

The bootloader leaves the PLL at **48 MHz** (HXTAL 8 MHz / 2 x 12). Measured at handoff:
RCU_CTL = 0x03008183, RCU_CFG0 = 0x0028000A, USART0 BAUD = 417 (115200 baud), USART0 CTL0 =
0x202C, PA9/PA10 in AF1.

Stock firmware switches to 108 MHz (its USART BAUD of 1875 for 57600 only fits 108 MHz). This
firmware stays at 48 MHz and sets 57600 baud itself. One side effect: the **10th harmonic of the
48 MHz clock is a spur at 480.000 MHz** (RSSI −65 to −80 dBm) that opens the squelch there.
Running at 108 MHz would move it.

### Bootloader and startup

The bootloader is entered in one of two ways:
- by switching the radio on while holding both side keys (white screen; everything at 115200
  baud);
- from running firmware: the stock program-mode command `UPDATE` resets into it, and the
  bootloader listens briefly for an INTO_BOOT frame at every start.

It accepts a `.kdhx` image over the serial line ([serial-interface.md](serial-interface.md)).

What an application has to do (`startup/startup_a36.S`, `src/main.c`):
- Mask interrupts and clear the NVIC first: the bootloader may leave USART interrupts enabled.
- Set VTOR to 0x08000400.
- Don't call GD's `SystemInit()`: it drops the clock to the 8 MHz RC oscillator.
- **Feed the free watchdog (FWDGT) regularly:** the bootloader leaves it running. This firmware
  feeds it in the main loop and inside every delay.
- The bootloader checks for the strings `PROGRAMJC37U` and `TalkpodA36Plus` in an image, and
  `talkpod-firmware` also reads a version as ` V<digits>.<digits>`. This firmware keeps all
  three in `.rodata.markers` (`src/main.c`).

### Board revision

Stock compares the 7 bytes at **0x08000300** (inside the bootloader) with `HWVER02`. The RF
switch wiring differs between HWVER02 and older boards (below). This firmware does the same and
handles both, but has only been tested on HWVER02.

## Pins

| Pin | Use |
|---|---|
| PA0 | analog input, ADC channel 0. **Not received audio:** sampled at 85 kHz it is a steady level, about 163/4095 idle and 28 with a local 1 kHz tone. It looks like a filtered audio level (VOX or similar). No raw audio reaches the MCU. |
| PA1 | battery voltage, ADC channel 1 |
| PA2 / PA3 / PC13 | BK4819 SCK / SDA (bidirectional) / CS. PA3 is also BK1080 SDA |
| PA4–PA7 | SPI flash CS / SCK / MISO / MOSI (SPI0, AF0) |
| PA8 | output: BK1080 power, set 10 ms after the BK1080 starts |
| PA9 / PA10 | USART0 TX / RX. **PA10 is also PTT** (low = pressed) |
| PA11 | voice prompt PWM: TMR1 channel 4, AF2 |
| PA12 | **speaker amplifier enable**, high = on |
| PA13 / PA14 | red (TX) / green (RX) LED. These are the SWD pins, so there is no SWD while the firmware runs |
| PA15 | output, left low by the bootloader; stock only clears it before resetting. Not a power hold; role unknown |
| PB0, PB1, PC14, PC15 | RF path switches (below) |
| PB2 / PB7 / PB10 / PB12 / PB13 / PB15 | LCD CS / backlight (high = on) / DC / reset / SCK (SPI1) / MOSI (SPI1) |
| PB3–PB6 | key matrix rows (outputs) |
| PB11 / PB9 / PB14 / PB8 | key matrix columns 0–3 (inputs with pull-ups) |
| PF6 | BK1080 SCL |
| PF7 | power switch sense: input with pull-up, low while on, high for 50 ms = switched off |

## BK4819

**Interface:**
- Bit-banged 3-wire SPI at about 250 kHz.
- A frame is CS low, 8 bits of register (bit 7 set = read), 16 bits of data, CS high, MSB first.
- The chip samples SDA on the rising SCK edge; for reads, SDA is switched to input after the
  address.
- PA3 is shared with the BK1080's data line, so the BK1080 clock (PF6) is held low so that it
  never sees an I2C START.

**Init:** stock's init sequence is in [stock-firmware.md](stock-firmware.md#bk4819) and in
`drivers/bk4819.c`. This firmware adds FSK interrupt enables: REG_3F = 0xF7FE instead of 0x07FE.

### REG_30 modes

Each mode is written after 0x0000 or 0x0200:

| Mode | Value |
|---|---|
| off | 0x0000 |
| receive | 0xBFF1 |
| transmit (mic) | 0xC1FE |
| local tone / beep | 0x0302 |
| transmit a generated tone (no mic) | 0xC3FA |
| microphone off during TX | clear bit 2 |

### Tuning, filters and audio

- **Frequency:** REG_38 (low word) / REG_39 (high word) in 10 Hz units.
- **Bandwidth:** REG_43 = 0x3028 wide, 0x4048 narrow.
- **Receive start (channel without tones):** REG_37 = 0x1F0F, frequency, REG_43, REG_78 / REG_4F
  squelch, REG_51 = 0x9000, REG_07 = 0x0471 then 0x21CD, REG_52 = 0x0292, REG_28 = 0x6B38 (RX
  audio expander; init leaves 0x6B00), REG_30 = 0x0200 then 0xBFF1.
- **Audio:** REG_47 = 0x6040 mute, 0x6140 FM, 0x6340 beep, **0x6740 AM**. REG_48 = 0xB3FF
  (AF output gain). The volume knob works without the firmware.

### RF path switches

`board_rf_path()` in `src/board.c`, from stock 0x8008710. Bands are below 300 MHz (VHF) and
300 MHz up (UHF).

| | HWVER02 | older boards |
|---|---|---|
| RX VHF | PB0 + PB1 high (stock drives PB1 as TMR3 PWM at full duty), PC14 low | PB1 high, PC14 low |
| RX UHF | PC14 high, PB0 low | PC14 high, PB1 low |
| TX VHF | BK GPIO2 (REG_33 bit 2) = 0, PC15 high | PB0 low, PC15 high |
| TX UHF | PC15 low, BK GPIO2 = 1 | PC15 low, PB0 high |
| off | REG_33 = 0, PB0 / PB1 / PC14 / PC15 low | same |

### Transmit

Stock key-up, from 0x800c000:
1. PA off.
2. 50 ms.
3. LEDs, RF path, speaker and AF mute off.
4. REG_37 = 0x1D0F, REG_30 = 0, frequency, REG_43, REG_51 / REG_52 = 0 (no tones), REG_7D mic
   gain (low 5 bits = 0x1A), REG_30 = 0x0200, then 0xC1FE.
5. REG_36 PA power.
6. Red LED, RF path to TX.

Key-down: PA off, REG_30 = 0, RF path off, receive.

This firmware does the same, except that the **RF path switches to TX before the BK4819 keys up,
and PA power is written last**.

**PA power:** REG_36 = bias << 8 | gain.
- The gain is 0xFF for high power and 0xD7 for low power.
- The bias comes from the radio's calibration: flash 0xF000 for high power and 0xF080 for low, one
  byte per frequency segment. Segment index by frequency:

  | MHz | below 130 | 130–145 | 145–166 | 166–200 | 200–300 | 300–400 | 400–420 | 420–455 | 455 up |
  |---|---|---|---|---|---|---|---|---|---|
  | index | 0 | 4 | 5 | 6 | 7 | 3 | 0 | 1 | 2 |

- A bias of 0x00 or 0xFF (uncalibrated) gives 0x007F, which is PA off. 0x007F is also what stock
  writes to switch the PA off.
- Values on the test radio: 476.9 MHz high 0xE2FF, low 0x52D7; 146 MHz high 0xF3FF.

The BK4819 synthesises **18–660 MHz and 840–1300 MHz**. Stock's own receive check is 18–660 and
840–1000 MHz.

### Squelch

- REG_78 = RSSI open << 8 | close; REG_4F = noise close << 8 | open.
- The chip's decision is **REG_0C bit 1**.
- **Thresholds:** from the calibration (below), exactly as stock computes them (0x80098f4). This
  firmware lowers wide-FM noise sensitivity by 15, and each level above 1 tightens noise by 3 and
  RSSI by 6 raw units (3 dB).
- **Open delay:** REG_4E bits 13:11 (stock 5). The bit rises about 40 ms + 5 ms per step after a
  retune onto a signal, 65 ms at stock's setting. RSSI and noise settle within 10–30 ms.
- **Readings:** RSSI is REG_67 (dBm = raw / 2 − 160); noise indicator REG_65 & 0x7F (10–32 on a
  signal, 50–80 without); glitch REG_63.
- **No signal on this radio:** RSSI reads −95 to −125 dBm, which is above the calibrated open
  threshold, so the noise criterion is what keeps the squelch closed.

### Frequency counter (search)

REG_3F / REG_59 / REG_70 / REG_58 = 0 / 0x2028 / 0 / 0, then REG_32 = 0x8245. When REG_0D bit 15
is clear the result is (REG_0D & 0x7FF) << 16 | REG_0E (10 Hz). REG_32 = 0x8244 stops it.

### Tone generator

- Tone 1 is REG_70 bit 15 (gain bits 14:8); tone 2 / FSK is bit 7 (gain bits 6:0).
- Frequency words are REG_71 (tone 1) and REG_72 (tone 2) = **Hz x 10.3244** (26 MHz crystal
  family).
- In TX mode 0xC3FA the tone replaces the microphone, and REG_71 can be rewritten on the fly
  without a phase jump (APRS uses this).
- Local beep: REG_30 = 0x0302, REG_47 = 0x6340.

### FSK modem and interrupts

See [data-modes.md](data-modes.md#the-bk4819-fsk-modem). Interrupt flags are polled:
- **REG_0C bit 0** = a flag is pending.
- Write REG_02 = 0, then read **REG_02**:

  | Bit | Flag |
  |---|---|
  | 15 | FSK TX finished |
  | 14 | FIFO almost empty |
  | 13 | RX finished |
  | 12 | FIFO almost full |
  | 3 / 2 | squelch found / lost |
  | 1 | FSK sync found |

- REG_3F enables them; nothing is wired to an MCU interrupt pin.

### SSB receive

The BK4819 has no SSB demodulator, but its audio output can be switched to baseband (REG_47 0x6540) with
the IF at 0 (REG_3D, 0x2AAB after reset) and AFC off (REG_73 bit 4), as the UV-K5's USB mode does. That
makes it a direct-conversion receiver, which should fold both sides of the tuned frequency onto the
same audio: one mode for both sidebands, with no rejection of the other one. Not yet confirmed on
this radio. To test without an SSB signal: key another radio without talking (a plain carrier), tune
1 kHz above it and 1 kHz below it, and listen for the 1 kHz tone on each side.

## External flash map

| Address | Contents |
|---|---|
| 0x000000–0x003FFF | 512 channels x 32 bytes (below) |
| 0x009000 | stock settings; squelch level 0–9 in the low nibble |
| 0x00F000–0x00F0B0 | per-radio TX power bias tables: 0xF000 high, 0xF080 low (8 segments each) |
| 0x00F0C0 / 0x00F0D0 | squelch RSSI open / close per level 0–9 |
| 0x00F0E0 | squelch correction above 400 MHz, per 10 MHz |
| 0x00F0F0 / 0x00F100 / 0x00F110 | corrections 130–200 / 200–300 / 300–400 MHz, per 5 MHz |
| 0x00F200 | battery thresholds, 6 bytes, in (ADC >> 4) units |
| 0x00F210 | noise: [5 + band] threshold (band 0 = 400 MHz up, 1 = 130–200, 2 = 200–300, 3 = 300–400), [9] hysteresis |
| 0x00F230 | band table, 5 bytes each: enable, low MHz, high MHz (BCD): 136–180, 400–520, 200–260, 350–400 |
| 0x020000–0x08FFFF | stock splash, icons, fonts |
| 0x110000–0x191FFF | stock voice prompts (index at 0x110000, see below) |
| 0x1F0000–0x1F1FFF | stock data (left alone) |

This firmware writes only its own region **0x110000–0x1EFFFF** and the stock programming window
**0x0000–0xB43F** (the PC's Read / Write, and its own settings from 1.13); `drivers/spiflash.c` refuses
anything else:

| Address | This firmware |
|---|---|
| 0x110000 | sound pack (`tools/soundpack.py`, replaces the stock prompts) |
| 0x120000–0x19CFFF | boot splash, 500 KB (0x19D000–0x1DFFFF free) |
| 0x8000, 0x9000 | stock's VFO and settings logs, which this firmware reads and writes in stock's format (1.14+) |
| 0x4000 / 0x5000 / 0x6000 / 0x7000 | settings logs (1.13+): main, APRS, pager, scramble key (32-byte records). In the window, which stock leaves empty, so Talkpod Manager's Read / Write carry them |
| 0x1E0000 / 0x1E1000 / 0x1E2000 / 0x1E3000 | where 1.0–1.12 kept them; 1.13 reads them once, while the window has none, and copies them over |

**Channel record**, 32 bytes, the same as CHIRP's `mml_jc8810` driver:

| Bytes | Field |
|---|---|
| 0–3 | RX frequency, little-endian BCD, 10 Hz |
| 4–7 | TX frequency, same format |
| 8–9 / 10–11 | RX / TX tone |
| 12 | signalling |
| 13 | PTT ID |
| 14 | scramble << 2 \| power (0 high, 1 low, 2 medium). Scramble: 0 off; CHIRP offers 4 SCRAM1 and 8 SCRAM2; Super Firmware 1.12 reads 4 x n as UV-K5 type n (1–10, 2500 + 100n Hz) and 60 as Rolling. Stock firmware doesn't use it |
| 15 | flags: bit 6 narrow, bits 5–4 ENCRY (stock's private code, 0 off), bit 3 busy lockout, bit 2 scan |
| 16–18 | code |
| 19 | (unused) |
| 20–31 | name, 12 characters |

An erased record (0xFF) is an empty channel.

**Voice prompts (stock):**
- **Index** at 0x110000: (u32 length, u32 offset) pairs, with the audio at offset + 0x90000. 143
  prompts numbered 16–158: 16–77 Mandarin, 78–148 English, 149–158 beeps.
- **Encoding:** standard IMA ADPCM, 16-bit predictor, **high nibble first**, about 8178
  samples/s. The Super 1.22 decoder clamps the index to 58 and the predictor to 12 bits, which
  clips this data.
- `tools/extract_voice.py` writes them as WAV files.

## Display

- **Panel:** ST7735S on SPI1 (mode 3, 12 MHz at 48 MHz: the panel's write limit is about
  15 MHz).
- **Geometry:** the controller RAM is 132 x 162; the visible 128 x 128 starts at **column 32**,
  with MADCTL = 0xA8.
- **Speed:** a full frame is 32 KB and takes about 22 ms.
- **Backlight:** PB7, on/off only.

## Keys

The rows are driven low one at a time and the columns read:

| | col 0 (PB11) | col 1 (PB9) | col 2 (PB14) | col 3 (PB8) |
|---|---|---|---|---|
| row 0 (PB3) | MENU | UP | DOWN | BACK |
| row 1 (PB4) | 1 | 2 | 3 | A/B |
| row 2 (PB5) | 4 | 5 | 6 | 0 |
| row 3 (PB6) | 7 | 8 | 9 | FM |

The top key, side key 1 and side key 2 pull columns 0, 1 and 2 low by themselves, so they are read
first with every row high.

**PTT** is PA10, the serial receive line. It stays in USART mode so that flashing works without
buttons. PTT counts as pressed when PA10 is low for 30 ms with no serial byte received in the last
50 ms, so cable traffic is never taken as PTT.

## Power, battery, LEDs

- **Power and volume knob:** it cuts power itself; PF7 tells the firmware first (high for 50 ms =
  switched off).
- **Battery:** PA1. Volts x 10 = (ADC >> 4) x 482 / 1000. The bar thresholds come from flash
  0xF200; the firmware warns on the last bar.
- **LEDs:** red PA13 (transmit), green PA14 (squelch open).

## Audio

- **Received audio:** BK4819 AF output, then the speaker amplifier, which PA12 enables.
- **Prompts:** PWM on PA11, TMR1 channel 4. At 48 MHz the period is 978 clocks with repetition
  count 5, which gives 8180 samples/s. Stock uses period 2201 at 108 MHz with DMA.
- **The programming cable cuts the internal speaker**, and probably the microphone: the jack's
  switch does it. Test audio with the cable unplugged.

## Serial

USART0 on PA9 / PA10 at the speaker/mic jack:
- the bootloader runs at 115200;
- stock program mode and this firmware run at 57600, 8N1.

Protocols are in [serial-interface.md](serial-interface.md).
