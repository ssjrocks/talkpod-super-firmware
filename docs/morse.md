# Morse code output (1.25+)

What the Morse screen transmits, exactly enough to write a decoder for it. The source is
[`src/morse.c`](../src/morse.c). The timing below was measured in the emulator (`tools/emu.py`); the
audio level and deviation haven't been measured on air yet.

## What goes out

**MCW**: an audio tone keyed on and off on a steady **FM** carrier. Any FM receiver tuned to the
channel hears it as beeps. Decode from the receiver's demodulated audio, not from the RF: the carrier
stays up for the whole message, and only the audio tone switches.

- **Carrier:** the selected slot's transmit frequency, FM. Bandwidth as the channel: narrow (12.5 kHz)
  or wide (25 kHz).
- **Tone:** **699.99 Hz** (the BK4819's tone generator, register word 0x1C3B; nominally 700 Hz).
  It is a pure sine at a constant level while on, and nothing while off.
- **Keying:** hard on/off, with no rise or fall shaping, so each edge can give a short click in the
  audio.
- **Audio path:** for this tone the transmitter switches its pre-emphasis and its 300 Hz high-pass
  off. A receiver with normal de-emphasis therefore plays it slightly quieter than a voice peak. The
  exact deviation isn't measured.

## One transmission, start to end

1. **Key-up:** the carrier comes on.
2. **500 ms of carrier with no tone**, so receivers' squelch and repeaters open.
3. **The message:** tone on/off per the timing below.
4. **After the last element:** 3 units of silence plus 150 ms.
5. **Key-down:** the carrier goes. A receiver plays a short burst of noise here as its squelch
   closes.

There is **no Roger and no squelch tail**, whatever the menu says: those settings are for voice
(1.25+; 1.24 sent them after Morse too).

So a message of N units takes `500 + N × unit + 150` ms from key-up to key-down. The radio refuses
to send one that would run past its TX timeout, and says how many seconds it needs.

Throughout, if the channel has **CTCSS** (67–254.1 Hz) or **DCS** (134.4 bit/s), that sub-audible signal
is on the carrier the whole time. Filter below about 300 Hz and it doesn't disturb a 700 Hz detector.

## Timing

One **unit** = `floor(1200 / WPM)` milliseconds (the PARIS standard: "PARIS " is 50 units).

| Element | Tone | Duration |
|---|---|---|
| dit | on | 1 unit |
| dah | on | 3 units |
| gap between elements of a letter | off | 1 unit |
| gap between letters | off | 3 units |
| gap between words | off | 7 units |

Speeds: 30 WPM to start with; the radio's UP / DOWN change it in steps of 5 from 5 to 40 WPM (the
setting goes back to 30 at power-off).

| WPM | unit (ms) | dit | dah | letter gap | word gap |
|---|---|---|---|---|---|
| 5 | 240 | 240 | 720 | 720 | 1680 |
| 10 | 120 | 120 | 360 | 360 | 840 |
| 15 | 80 | 80 | 240 | 240 | 560 |
| 20 | 60 | 60 | 180 | 180 | 420 |
| 25 | 48 | 48 | 144 | 144 | 336 |
| **30** | **40** | **40** | **120** | **120** | **280** |
| 35 | 34 | 34 | 102 | 102 | 238 (really 35.3 WPM, from the whole-ms unit) |
| 40 | 30 | 30 | 90 | 90 | 210 |

**Accuracy:** each edge is scheduled at an absolute time on a 1 ms clock, so errors don't add up over a
message. An edge can come a few ms late when the radio's screen updates; the next edge is on time
again. In the emulator, every element and gap of PARIS at 30 WPM was exact to the millisecond.
Through a real receiver, expect extra smearing at the edges from its audio filters and AGC.

### Example: "PARIS" at 30 WPM

Tone on/off durations in ms, starting at the first tone (after the 500 ms lead):

```
P  .--.   on 40, off 40, on 120, off 40, on 120, off 40, on 40,  off 120
A  .-     on 40, off 40, on 120,                                 off 120
R  .-.    on 40, off 40, on 120, off 40, on 40,                  off 120
I  ..     on 40, off 40, on 40,                                  off 120
S  ...    on 40, off 40, on 40, off 40, on 40,                   then 120 + 150 ms to key-down
```

## Characters

Lower case is sent as upper case. Characters not in this table are skipped: they add no gap of their
own. Only a space makes a word gap, several spaces make one gap, and spaces before the first letter
make none.

| | | | | | |
|---|---|---|---|---|---|
| A `.-` | B `-...` | C `-.-.` | D `-..` | E `.` | F `..-.` |
| G `--.` | H `....` | I `..` | J `.---` | K `-.-` | L `.-..` |
| M `--` | N `-.` | O `---` | P `.--.` | Q `--.-` | R `.-.` |
| S `...` | T `-` | U `..-` | V `...-` | W `.--` | X `-..-` |
| Y `-.--` | Z `--..` | 0 `-----` | 1 `.----` | 2 `..---` | 3 `...--` |
| 4 `....-` | 5 `.....` | 6 `-....` | 7 `--...` | 8 `---..` | 9 `----.` |
| `.` `.-.-.-` | `,` `--..--` | `?` `..--..` | `'` `.----.` | `!` `-.-.--` | `/` `-..-.` |
| `(` `-.--.` | `)` `-.--.-` | `&` `.-...` | `:` `---...` | `;` `-.-.-.` | `=` `-...-` |
| `+` `.-.-.` | `-` `-....-` | `_` `..--.-` | `"` `.-..-.` | `@` `.--.-.` | |

There are no prosigns (AR, SK...), and `$` isn't sent. A message is at most 87 characters.

## Decoding it

A decoder that works on this signal:

1. **Input:** the receiver's audio (speaker, line out or an SDR's FM demodulator), mono, 8 kHz or more.
2. **Tone detection:** a Goertzel filter or narrow band-pass at 700 Hz, about ±50–100 Hz wide, over
   windows of about a quarter unit. That is 5–10 ms at 30 WPM, and 5 ms covers up to 40 WPM. Smooth
   the magnitude and threshold it with hysteresis, either against a noise floor measured in the
   500 ms lead-in or with an adaptive mid-point between the on and off levels.
3. **Unit estimate:** use the shortest common on-duration (the dits); it should be close to
   1200 / WPM. Adapt it as the message goes, or take it from the known WPM for testing.
4. **Classify:**
   - on shorter than 2 units is a dit, longer is a dah;
   - off shorter than 2 units is inside a letter, 2–5 units ends a letter, longer than 5 units is a
     word space.
5. **Ignore:**
   - energy below about 300 Hz (CTCSS, DCS, the 55 Hz tail);
   - the noise burst at key-down.
   The message is complete when the carrier drops.
6. **Test:** send known texts (PARIS, the alphabet, digits, punctuation, text with several spaces) at
   5, 30, 35 and 40 WPM, and compare the decoded text and the measured unit with the tables above.
