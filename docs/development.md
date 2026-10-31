# Development

## Source layout

| Path | What |
|---|---|
| `startup/startup_a36.S`, `link.ld` | vector table, reset (masks IRQs, VTOR = 0x08000400), memory: 63 KB flash, RAM up to the stack at 0x20003E48 |
| `src/main.c` | init order, main loop (10 ms tick for keys, power switch, UI), image markers |
| `src/version.h` | `FW_VERSION` |
| `drivers/` | BK4819 (SPI, RX / TX / tones / FSK modem / interrupts), LCD, keypad, SPI flash (writes limited to 0x110000–0x1EFFFF), ADC |
| `src/board.c` | GPIO, RF path switching, LEDs, speaker users, power switch |
| `src/radio.c` | receive: tuning, calibrated squelch, audio, monitor, search |
| `src/watch.c`, `src/scan.c` | dual watch, channel (and unused VFO) scan |
| `src/tx.c` | transmit: allowed ranges, PA calibration, key-up / key-down |
| `src/msg.c`, `src/aprs.c`, `src/pocsag.c` | data modes ([data-modes.md](data-modes.md)) |
| `src/ui.c` | every screen, keys, menu, settings record |
| `src/voice.c` | sound pack player (TMR1 PWM, IMA ADPCM) |
| `src/splash.c` | boot splash player |
| `src/settings.c` | settings logs in flash |
| `src/progmode.c`, `src/shell.c`, `src/debug.c` | serial: program mode, shell, interrupt-driven RX buffer ([serial-interface.md](serial-interface.md)) |
| `gfx/` | text, icons and fonts (bitmap X11 fonts; anti-aliased DejaVu digits) |
| `lib/gd32f3x0/` | vendored GigaDevice library (clocks, GPIO, SPI, ADC only; see hardware.md) |
| `tests/bare_*.S` | the first bare-metal images used to find out how the bootloader starts firmware |

## Building

```bash
make            # build/a36se-fw.elf / .bin / .kdhx; fails if the image passes 63 KB
make flash      # talkpod-firmware flash --yes build/a36se-fw.kdhx
```

`arm-none-eabi-gcc` with newlib nano, `-Os`, sections garbage-collected. Space used at 1.0: about
43 KB of 63 KB flash; 6.7 KB of RAM static, plus a 1.5 KB stack reserve. The largest C library
functions are avoided (newlib's `strcmp` alone is 730 bytes).

## Emulator

`tools/emu.py` runs the firmware in Unicorn (`pip install -r tools/requirements-emu.txt` into a
venv) with models of the peripherals that matter:
- LCD (written to a PNG);
- keys, with a script of presses;
- BK4819 registers, with signals on chosen frequencies, scheduled register values, FSK packets
  delivered to the FIFO, and FSK sends that take their real time;
- SPI flash (a 2 MB image; program / erase; can be saved);
- ADC;
- the DWT cycle counter;
- the bootloader's board tag.

```bash
.venv/bin/python tools/emu.py build/a36se-fw.elf --flash FLASH.bin --ms 4000 \
    --keys 1500:MENU:80,2000:PTT:1500 --bk --pins PC15,PA13 --png out.png
```

`--keys START:KEY:MS,...` (keys as `key_name()` spells them), `--bk` logs every BK4819 access with
cycle stamps, `--pins` logs output changes among them. Also:
- `--signal-at FREQ_10HZ`
- `--bkreg-at MS:REG=VAL`
- `--fsk-rx MS:HEX`
- `--flash-out FILE`
- `--voice` (lists the sounds played with their PWM swing, by running the timer interrupt)
- `--hwver 02`

Time is simulated (about 48 MHz at two cycles per instruction), so timings are approximate.

Helpers:
- `tools/aprs_emu.py`:
  - `cfg` puts an APRS callsign into a flash image;
  - `rx SRC DEST INFO` makes the raw demodulator capture of a frame;
  - `tx LOG` decodes the AFSK the firmware sent (from REG_71 writes and cycle stamps).
- `tools/pocsag_emu.py`: `rx RIC TEXT [numeric]` makes a capture; `tx LOG` decodes a sent page from
  the FIFO writes.
- `tools/splash.py GIF --image IN OUT` puts a splash into a flash image.
- `tools/stock_emu.py`: drives program mode through the emulator as CHIRP does, on a flash image, and
  checks the result by stock's rules. `emu.py --serial-in` / `--serial-out` carry the bytes.
- `tools/roll.py`: the rolling scrambler's reference path. `cfg` puts a key and rolling channels into
  a flash image, `burst` makes an `--fsk-rx` capture, and `check LOG KEY` compares the REG_71 writes of
  a run against the path.

## Other tools

| Tool | |
|---|---|
| `tools/flash_backup.py` | 2 MB flash backup over the shell (`X`), CRC-checked, about 6.5 min |
| `tools/soundpack.py BACKUP` | builds the ten-sound pack from the backup's stock prompts and writes it (erases 0x110000–0x191FFF) |
| `tools/extract_voice.py BACKUP` | all stock voice prompts as WAV files |
| `tools/splash.py` | boot splash: convert, upload, preview, remove ([serial-interface.md](serial-interface.md#boot-splash)) |
| `tools/re/call_stock.py` | calls one function of a decrypted stock image in Unicorn and logs its BK4819 / GPIO activity ([stock-firmware.md](stock-firmware.md)) |
| `tools/re/stockdis.py` | disassembly helper for the stock image |
| `tools/screens.sh` | renders the README's picture, `docs/ui/screens.png`, from the build in the emulator |
| `tools/mockup.py`, `tools/mkfont.py`, `tools/mkaafont.py` | UI mockups, bitmap and anti-aliased font tables |
| `tools/encrypt.c` | `.kdhx` scrambling |

The stock firmware image, flash backups and extracted prompts are vendor data and per-radio data.
They are not in this repository (`.gitignore`): make your own from your radio.

## What has been checked where

| Feature | On the radio | Emulator only |
|---|---|---|
| Display, keys, backlight, power switch | ✓ | |
| FM receive and audio, squelch, monitor | ✓ (test signal in a shielded enclosure) | |
| Search, channel scan, dual watch | ✓ | |
| Settings across power-off | ✓ | |
| Sounds | ✓ | |
| FM transmit, power levels, blocked ranges | ✓ voice heard on another radio (1.11); the ranges in the emulator | |
| TX time-out | | ✓ |
| AM receive | built, not yet checked with an AM signal | |
| UV-K5 messages | modem registers read back | ✓ send, receive, receipts |
| APRS send | | ✓ frames decode with valid FCS |
| APRS receive | | ✓ with ideal captures; the BK4819 demodulator is untested |
| POCSAG send / receive | | ✓ |
| Scrambler, fixed (UV-K5 types) | ✓ heard on another radio (types 1, 2): weak | ✓ register values |
| SSB receive | | ✓ registers, PTT refused; both sidebands unproven |
| Rolling scrambler | ✓ heard on another radio (without the key) | ✓ burst, glide sent and received against `tools/roll.py`, other key refused |
| Settings in the window (1.13): copied from older firmware's place, written by Talkpod Manager | ✓ read, written and applied without a restart | ✓ records from `tsf_settings.py` applied |
| Boot splash upload, `i` / `G` | ✓ | ✓ playback |

## Known issues and ideas

- **480 MHz spur:** the 48 MHz MCU clock's 10th harmonic. Switching to 108 MHz as stock does would
  move it, and give more headroom.
- **On-air checks of the 1.1 channel features:** CTCSS / DCS tones are set exactly as stock does,
  emulator-tested, but not yet heard by another radio.
- **Scrambler:** works on air (1.11), but plain inversion leaves words partly understandable. It is
  the UV-K5's method (REG_31 bit 1, REG_71); stock has no voice scrambler.
- **Still to come from stock:** Remote Scan (unknown), DTMF side tone, tone scanning.
- **Channels:** not editable on the radio, and program mode has no channel read/write.
- **On-air checks:** messages with a UV-K5; APRS and POCSAG with an SDR (multimon-ng, Direwolf) or a
  pager. That includes POCSAG polarity and bit order, AFSK and FSK deviation, and whether APRS
  receive works at all.
- **VFO scan:** exists in `src/scan.c` but is not offered: it skipped past signals in testing and
  was set aside in favour of the channel scan.
- **No restore tool** for writing a full flash backup back.
