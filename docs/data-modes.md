# Data modes: messages, POCSAG

Both use the BK4819's built-in FSK modem. There is one modem, so the receiver listens for one
format at a time:
- POCSAG while the Pager screen is open;
- UV-K5 messages everywhere else.

Every transmission goes through `tx_start()`, so the blocked ranges apply to receipts, acks and
beacons too.

## The BK4819 FSK modem

Register use, from UV-K5 firmware sources (which name most bits) and from this firmware:

| Register | Use |
|---|---|
| REG_58 | <15:13> TX mode (0 direct FSK, 1 FFSK 1200/1800, 3 FFSK 1200/2400), <12:10> RX mode (0 direct FSK, 4 FFSK 1200/2400, 7 FFSK 1200/1800), <9:8> RX gain, <5:4> preamble type (0 automatic, 2 0x55, 3 0xAA), <3:1> RX bandwidth (0 FSK 1.2k, 1 FFSK 1200/1800, 4 FSK 2.4k and FFSK 1200/2400), <0> enable |
| REG_59 | <15> clear TX FIFO, <14> clear RX FIFO, <13> scramble, <12> RX on, <11> TX on, <10> invert RX data, <9> invert TX data, <7:4> preamble bytes − 1, <3> 4-byte sync (else 2) |
| REG_5A / 5B | sync bytes 0, 1 / 2, 3 (byte 0 sent first) |
| REG_5C | 0x5625: no CRC (bit 6 would enable one) |
| REG_5D | <15:8> packet length in bytes; 8 bits, so at most 255 |
| REG_5E | <9:3> FIFO almost-empty threshold, <2:0> almost-full threshold |
| REG_5F | FIFO, 16 bits per access; the low byte goes first |
| REG_70 | tone 2 / FSK enable (bit 7) and gain (bits 6:0, 96 used) |
| REG_72 | bit rate as a tone-2 frequency word: **bit/s x 10.3244** (1200 → 12389) |
| REG_40 | deviation, low 12 bits; FSK sends use 1300 wide / 1200 narrow (UV-K5 values), voice 1234 |
| REG_2B | 0x0005 while sending: TX 300 Hz high-pass and pre-emphasis off |

**Sending:**
1. Transmitter on.
2. Microphone off (REG_30 bit 2).
3. Deviation and REG_2B as in the table above.
4. Configure the modem and clear the FIFOs.
5. Load the FIFO and set REG_59 bit 11.
6. Wait for REG_02 bit 15.

This firmware loads 32 words and tops up 16 at a time on "almost empty" (REG_02 bit 14), so
packets up to 255 bytes work whatever the FIFO size (36 words are known to fit).

**Receiving:**
- Set REG_59 bit 12, then poll the flags: sync found (REG_02 bit 1), a word is in the FIFO (bit 12,
  threshold 1 word), packet complete after REG_5D bytes (bit 13).
- Reading one word per "almost full" flag can fall behind when the main loop is busy (the flags
  latch, they don't count). So after "complete" the firmware reads the remaining words up to the
  packet length.
- The RX FIFO is cleared and re-armed after every retune (`bk4819_rx()`), so dual watch and data
  reception get along.

**Voice transmit:** the modem must be off (REG_58 = 0, REG_70 = 0). While it's on, the microphone
doesn't reach the modulator and the radio sends a silent carrier. Firmware 1.0–1.10 did that
(found on air, fixed in 1.11). `bk4819_tx()` switches the modem off, as the UV-K5 messenger does on
every transmit. The first `bk4819_rx()` afterwards sets it up again. A data send configures the
modem itself after `bk4819_tx()`.

## Rolling scrambler

`src/roll.c`, with `tools/roll.py` as the reference. The BK4819 has one voice scrambler: inversion
around a frequency (REG_31 bit 1, REG_71 = Hz x 10.3244). The MCU never sees audio, so it can't do
anything stronger. What it can do is move that frequency as you speak. A path both radios work out
from the key keeps it gliding:
- **Path:** xorshift32, seeded with FNV-1a(key) ^ IV x 2654435761 (| 1). Each segment glides to a
  random frequency in 2600–3500 Hz, taking |change| / 3 + 40 + 0..79 ms. So the frequency never
  changes faster than 3 Hz per ms.
- **Why gliding:** a timing error of t ms between the two radios only shifts the descrambled pitch by
  up to 3t Hz. Hopping would garble a slice of every hop instead.
- **Burst:** at PTT, on the text-message modem (Msg speed), with its own sync word **52 CB 7A 1D**,
  so UV-K5 messenger radios never sync on it. 8 bytes: 01, IV (4 bytes, little-endian), key check
  (the top byte of FNV-1a(key)), check (2 bytes: the low half of FNV-1a over bytes 0–5). At 1200
  bit/s that is about 0.19 s with the preamble; the sender then turns the modem off and the mic
  on, and glides from the moment the burst ended.
- **Receiving:** a rolling channel arms the modem for that sync word, with the packet 10 bytes long
  (2 more than sent, as messages are received). The burst's end is taken from the two times polling
  saw (sync + 8 bytes, end of packet − 2 bytes, the earlier one minus 3 ms). The receiver glides
  until its carrier has been gone for 0.4 s or it retunes.
- **Checked:** in the emulator, the sent and received paths match `tools/roll.py` to within 3 Hz,
  and a burst for another key is refused. Not yet checked: two radios on air.

## UV-K5 messages

`src/msg.c`. This is the format of the messenger in
[kamilsss655's UV-K5 firmware](https://github.com/kamilsss655/uv-k5-firmware-custom) (2024), the
most widely used one:

| | |
|---|---|
| Modulation | FFSK 1200/1800 at 1200 bit/s (REG_58 0x3F03), or direct FSK at 700 or 450 bit/s (REG_58 0x0301) |
| Framing | 16 preamble bytes, sync **30 72 57 6C**, 44 bytes, no CRC |
| Packet | type (100 message, 101 encrypted, 102 receipt), 30 bytes of text padded with zeros (29 characters at most), 13-byte nonce (encrypted only, zeros otherwise) |
| Receive length | 46 bytes (rounded up to even, + 2, as the UV-K5 does) |
| Receipt | 700 ms after a message: type 102, text five 0xFF bytes; the sender marks its newest sent message delivered |

Encrypted messages (ChaCha20 with a key derived from a password) are listed, not decrypted.
joaquimorg's older messenger uses another sync (55 55 55 AA) and layout, so it doesn't interoperate.

**Verified:** in the emulator, the FIFO contents of a sent message, decoding of a UV-K5 packet, the
receipt and its effect; on the radio, the modem registers. **Not yet:** an exchange with a real
UV-K5.

## APRS (removed in 1.33)

1.12 to 1.32 sent APRS beacons and messages (Bell 202 tones from the tone generator; Direwolf decoded
them on air) and tried to receive APRS with the modem. Receiving cannot work on this board, so APRS
was taken out in 1.33. Tested on air with a HackRF on 2026-11-18:
- The BK4819 has FFSK 1200/1800 and 1200/2400 modes, not Bell 202's 1200/2200 Hz. In either mode it
  sometimes syncs on the flags, then gives slipped flags or one fixed byte instead of the packet.
- Its direct FSK receiver run as a fast 1-bit sampler of the audio (4400-6000 bit/s) follows the
  tones on average, but 27-34 % of the per-bit tone decisions are wrong at every rate and bandwidth
  tried, and one sync gives at most 255 bytes.
- Receiving would need the received audio on an MCU ADC pin, a hardware change. UV-K5 firmware
  developers reached the same point (github.com/piotr022/UV_K5_playground/issues/20).

The code and the test notes are in the git history up to 1.32.

## POCSAG

`src/pocsag.c`, ITU-R M.584 at 512, 1200 or 2400 bit/s, on the modem's **direct FSK** mode
(REG_58 = 0x0331, or 0x0339 at 2400 with the 2.4k filter). REG_72 sets the rate.

| | |
|---|---|
| Codeword | 32 bits MSB first: flag (0 address, 1 message), 20 data bits, BCH(31,21) check bits (generator 0x769), even parity |
| Batch | sync codeword **7CD215D8** + 8 frames x 2 codewords; idle codeword 7A89C197 |
| Address | a pager with RIC r listens in frame r & 7; its address codeword carries r >> 3 and 2 function bits |
| Text | alphanumeric: 7-bit characters, LSB first, function 3. Numeric: 4-bit digits `0123456789*U -)(`, LSB first, function 0 |

**Sending:**
- Preamble: the chip sends 16 preamble bytes (type 0xAA) and a 4-byte "sync" that is also 0xAA, and
  the payload begins with 50 more 0xAA bytes. That makes 70 bytes, 560 of POCSAG's 576 preamble
  bits.
- Then the batches, each with its sync codeword. The message ends with an idle codeword.
- At most 3 batches fit in the modem's 255-byte packet, which allows about 40 alphanumeric
  characters even for RICs in frame 7; typing is limited to 29.
- **Polarity** (1.28): the chip's direct FSK is the other way round from standard POCSAG, so the
  payload (preamble bytes and codewords) is inverted before it goes into the FIFO. Measured on air
  with 1.27: multimon-ng decoded the radio's pages only with `-i`. "Pager invert" sends them
  uninverted.
- **Deviation** (1.28): REG_40 1005, for +-4.5 kHz. 1.27 used the messages' 1300, measured at
  +-5.8 kHz on a wide channel.
- The text's last codeword is padded (spaces or NUL bits); 1.27 and earlier added up to two more
  codewords of NULs.

**Receiving:**
- The chip syncs on 7CD215D8 itself and captures 254 bytes (63 codewords, almost four batches).
  Later sync codewords are expected every 17th codeword.
- Each codeword is checked and single-bit errors corrected (by trying every bit).
- Decoding stops at an uncorrectable codeword (the transmission is over).
- **Polarity** (1.28): the chip captures standard POCSAG inverted (measured on air: a HackRF's
  page arrived with every bit inverted), so it listens for the inverted sync codeword (REG_5A/5B
  832D EA27) and inverts the capture before decoding; "Pager invert" swaps both. Until 1.27 the
  receiver alternated polarity every 100 ms and re-armed each time, which lost sync codewords.
- Dual watch keeps to the selected slot while the screen is open (1.28).
- The chip reports the packet complete only when the carrier goes (seen on air with 1.28), so a
  capture is decoded when its 254 bytes are in, or 0.3 s after they should be (1.29). 1.28 and
  earlier dropped a capture undecoded after 5 s, which at 512 bit/s was every one.
- The FIFO is read 4 words per almost-full flag (REG_5E threshold 4, as UV-K5 AirCopy does), 1.30.
  A word at a time, a capture fell behind and once read a word twice, which shifted the rest; the
  decoder also steps over one such repeated word.

**Verified:** in the emulator, sent FIFO contents decode to the right RIC and text
(`tools/pocsag_emu.py tx`). Received captures decode numeric, alphanumeric, multi-batch and
bit-error cases. **Not yet:** on air. The chip's bit order within a FIFO byte (assumed MSB first,
as the sync bytes go), the FSK polarity and the deviation (REG_40 1300) must be checked against a
real decoder (multimon-ng, PDW) or pager.
