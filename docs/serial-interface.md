# Serial interface

The programming cable (K-plug USB serial adapter) reaches USART0 through the speaker/mic jack:
PA9 TX, PA10 RX (shared with PTT). Three things talk on it:

| Who | Speed | Use |
|---|---|---|
| Bootloader | 115200 8N1 | writing firmware |
| Program mode (stock, and this firmware's subset) | 57600 8N1 | identifying the radio, restarting into the bootloader |
| This firmware's shell | 57600 8N1 | flash access, boot splash, sounds, diagnostics |

Program mode and the shell share the port. Bytes go to both, and the program-mode handshake is
recognised anywhere in the stream.

## Program mode

`src/progmode.c`. Compatible with what `talkpod-firmware` (Talkpod Manager) and CHIRP send:

| PC sends | Radio answers |
|---|---|
| `PROGRAMJC37U` | 0x06 (log output is muted until `E` or 1 s of silence) |
| `F` | 8-byte identification: **`A36CFW02`** for this firmware 1.1+ (`A36CFW01` was 1.0; stock sends its own fingerprint) |
| `R` addr(2, big-endian) len(1) | `R` addr len, then len bytes from flash at addr |
| `W` addr(2, big-endian) len(1), then len bytes | 0x06 (0x15 if the range is outside the window) |
| `E` | leaves program mode |
| `UPDATE` | 0x06, then a reset into the bootloader, ready for an INTO_BOOT frame |

`R` and `W` (1.1 and later) address the external flash directly, from 0 to 0xB43F, as stock does:
- channels at 0x0000 (512 × 32 bytes);
- VFO data at 0x8000;
- settings at 0x9000;
- DTMF at 0xA000;
- ANI codes and custom names at 0xB000.

That is the memory map of CHIRP's `mml_jc8810` driver, so CHIRP's A36plus driver reads and writes
this firmware unchanged once it accepts the ident. Talkpod Manager does this, and its images move
between stock and Super Firmware.

Writes collect in a 4 KB sector buffer. A sector is erased and programmed only if its data changed,
once, when a write moves to another sector, on `E`, or after 1 s of silence.

## Flashing

`talkpod-firmware` in Talkpod Manager implements this; OpenRTX's `rtxflash` does the same.

- **Entry:**
  - Automatic: at 57600, `PROGRAMJC37U` → 0x06, then `UPDATE` → 0x06. Reopen at 115200 and send
    INTO_BOOT.
  - Manual: switch on while holding both side keys (white screen), everything at 115200.
- **Frames:** `AA | cmd | arg | length (2, big-endian) | data | CRC-16/XMODEM (2) | EF`, with the
  CRC over cmd..data. The reply has the same cmd, and arg 0x06 (OK) or an error code 0xE1–0xE5.
- **Sequence:**
  1. INTO_BOOT (0x42).
  2. HELLO (0x01, data "BOOTLOADER").
  3. COUNT (0x04, the number of 1 KB chunks).
  4. DATA (0x03, arg = chunk index, 1024 bytes, zero-padded).
  5. END (0x45).
- **`.kdhx`:** the binary with some 1 KB blocks XOR-scrambled.
  - Block i (not the first two or the last two) is XORed with `KDHT` if i % 3 == 1, with `RBGI` if
    i % 3 == 2.
  - A byte is left alone if it is 0x00, 0xFF, the key byte or the key byte XOR 0xFF.
  - `tools/encrypt.c` (built by the Makefile) does it both ways.
- **Checks:** the image must contain `PROGRAMJC37U` and `TalkpodA36Plus`. `talkpod-firmware` reads
  the version from ` V<n>.<n>` (here ` V1.00`) and refuses images over 66 KB for this model.

## The shell

`src/shell.c`.
- **Commands:** one per line, ended by CR (or LF); arguments separated by spaces; hex where noted.
- **Replies:** lines ended by CRLF.
- **Log lines:** the radio also prints them (keys pressed, `message: ...`, `APRS ...`, `page ...`,
  `TX on`). Pick replies out by their content. Log lines are muted while program mode is active.

| Command | Reply |
|---|---|
| `i` | `fw=TSF ver=1.10 board=HWVER02 splash=00120000+0007D000 splash_fmt=A36G,A36H`: identifies this firmware, where the splash goes (address + size, hex) and which splash headers it reads |
| `G` | plays the boot splash now, then `ok`, or `no splash`. The screen returns to what it showed |
| `E ADDR` | erases the 4 KB sector at ADDR (hex): `ok`, or `refused` outside 0x110000–0x1EFFFF |
| `W ADDR LEN` | then LEN raw bytes (1–256) and their CRC-16/XMODEM, big-endian. Programs them at ADDR (hex) into erased flash: `ok`, `crc`, `refused` or `timeout` (bytes not all there within 2 s). Wait ~50 ms after the command line before sending the bytes |
| `X ADDR LEN` | LEN raw bytes of flash from ADDR (hex; LEN decimal, up to 65536), then their CRC-16/XMODEM, big-endian. No line ending |
| `x ADDR LEN` | hex dump (LEN up to 256) |
| `P ID` | plays sound ID from the sound pack |
| `r RR` / `w RR VVVV` | read / write a BK4819 register (hex). A REG_30 value with the PA gain bit set is refused |
| `R ADDR` | reads a 32-bit word anywhere, e.g. a peripheral register |
| `d` | main BK4819 registers and GPIO outputs |
| `f HZ` | tunes the selected slot (as VFO) to HZ |
| `s L` | squelch level L (0–9). **Note:** `s` alone sets 0 |
| `t HZ` / `t 0` | continuous local tone (no RF) / stop |
| `b` | 1 kHz beep |
| `m HZ N` | retune to HZ (N: 1 = narrow), log squelch bit, RSSI and noise every 5 ms for 200 ms |
| `v LO HI ST N` | survey LO..HI Hz in ST Hz steps; any byte stops it |
| `V` | sound player state |

CRC-16/XMODEM: polynomial 0x1021, initial value 0, no reflection, no final XOR.

The firmware only ever writes inside **0x110000–0x1EFFFF**. The channels, the calibration and stock
data cannot be changed from the shell.

## Boot splash

The splash is an animation stored in the external flash at **0x120000–0x19CFFF (500 KB)**; firmware 1.0
allowed 768 KB. It is
played once at power-on, and any key skips it. Changing it needs nothing but the shell above, so
**Talkpod Manager can upload GIFs** with the steps below. `tools/splash.py` is the reference
implementation; it is self-contained (Pillow and pyserial only) and can be imported or copied:

```python
import splash                                   # tools/splash.py
blob, info = splash.convert("logo.gif")         # A36G bytes; info: frames, ms, bytes, capacity
with splash.Radio("/dev/ttyUSB0") as r:
    r.info()                                    # {'fw': 'TSF', 'ver': '1.10', 'splash': '00120000+0007D000', ...}
    r.upload(blob, progress=lambda done, total: ...)
    r.play()                                    # preview
    # r.remove()                                # no splash at all
```

### Format (A36G / A36H)

Little-endian throughout.

```
offset 0   "A36G"                 magic
       4   u16 frames
       6   u16 background         RGB565, drawn behind the first frame
       8   u32 total bytes        header included; at most 0x7D000
      12   frames...

or, firmware 1.1 and later:
offset 0   "A36H"                 magic
       4   u16 frames, u16 background, u32 total bytes   as A36G
      12   u8  plays              the frames are played this many times (stored once)
      13   3 reserved bytes
      16   frames...

frame:     u16 delay_ms           how long this frame stays before the next one is drawn
           u8  x, y, w, h         the rectangle this frame redraws (w = 0: nothing changes)
           u16 colours            1..256
           u16 palette[colours]   RGB565
           u32 data_bytes
           u8  data[data_bytes]   PackBits over palette indices of the w x h rectangle, row by row:
                                  n < 128   -> n + 1 literal indices follow
                                  n >= 128  -> the next byte is an index, repeated n - 125 times (3..130)
```

The screen is 128 x 128. A frame must fit: x + w ≤ 128 and y + h ≤ 128. The first frame normally
covers the whole screen. Storing only what changed keeps files small and playback fast, at about
22 ms for a full frame. Frames are drawn as they are decoded, then the player waits out the
frame's delay. If the header is missing or wrong, the radio boots without a splash.

**Converting a GIF** (what `splash.convert()` does):
1. Composite each frame (GIF disposal included) and fit it into 128 x 128, centred on the
   background colour.
2. Reduce to 256 colours if needed.
3. Find the rectangle that changed since the previous frame.
4. Build its palette and PackBits-code it.

Frame delays come from the GIF (20 ms at least). Conversion stops at 15 s of animation by default
(`max_ms`).

### Uploading

1. **Check the firmware:** send `i`. A line starting `fw=TSF` means this firmware and gives the
   splash area. With older or stock firmware, there's no answer. Program-mode ident `A36CFW0x` also
   identifies it. The radio must be switched on and not in program mode.
2. **Erase** every 4 KB sector the splash will cover: `E 120000`, `E 121000`, ... Each answers `ok`.
3. **Write** the bytes 256 at a time: `W 120000 256`, ~50 ms pause, 256 bytes, 2 CRC bytes; expect
   `ok`. Retry a chunk that gets `crc` or `timeout`. Rewriting the same data is harmless.
4. **Verify** with `X 120000 4096` and so on: compare the bytes and check the trailing CRC.
5. **Preview** with `G` (optional). The new splash also plays at the next power-on.

A 160 KB animation takes about 90 s at 57600 baud, half of it the read-back.

**Removing** the splash: `E 120000` (the header is gone, the radio boots straight to the main
screen).

## Other stored data

**Settings** (`src/settings.c`): logs of 32-byte records, one 4 KB sector per area: 0x1E0000 main,
0x1E1000 APRS, 0x1E2000 pager.

| Bytes | Field |
|---|---|
| 0–1 | magic 0xA36C (u16) |
| 2 | payload length |
| 3 | 0 |
| 4–29 | payload, zero-padded |
| 30–31 | CRC-16/XMODEM of bytes 0–29 (u16) |

The newest valid record wins, and the sector is erased only when full. A record from older firmware
is shorter; the fields it lacks keep their defaults.

**Sound pack** (`tools/soundpack.py`) at 0x110000:
- header: "SNDP", u16 count, u16 sample rate;
- then count x {u16 id, u16 0, u32 offset, u32 length};
- then IMA ADPCM data (high nibble first, as the stock prompts).

The ids are the stock prompt numbers: 120 low voltage, 121 channel mode, 122 frequency mode,
137 menu, 138 high, 139 low, 149 key, 151 cancel, 153 power-on, 156 open.
