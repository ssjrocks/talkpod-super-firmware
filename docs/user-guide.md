# User guide

## Main screen

```
 ▂▄▆ DW        🔒 ✉   7.8V ▮▮▮      status bar: signal / TX, dual watch, scan, lock, new message, battery
┌──────────────────────────────┐
│ CH 11  CB11 CALL        N H  │   slot A (selected: highlighted, frequency in amber)
│ 476.67 50 MHz                │   chip: CH n / VFO A, green while receiving, red while transmitting
│ ▮▮▮▮▮▮▮▮▯▯▯▯▯▯▯▯              │   signal meter (red TX power while transmitting)
├──────────────────────────────┤
│ CH 40  CB40 ROAD        N H  │   slot B
│ 477.40 00 MHz                │
│ ▯▯▯▯▯▯▯▯▯▯▯▯▯▯▯▯              │
└──────────────────────────────┘
 SQ1  12.5k                         hint line: squelch and step, or what the keys do now, or a message
```

Each slot is a **channel** (from the radio's channel memory) or a **VFO frequency**. The marks at
the right are the bandwidth (W wide, N narrow, AM) and the TX power (H high, L low). A channel
brings its own bandwidth and power; channels in 108–137 MHz and on 27 MHz CB receive AM. With **dual watch** on,
the receiver alternates between the two slots (about 110 ms each). It stays on a slot while it
hears a signal and 1 s after.

The **signal meter** has 16 segments of 6 dB, one S-unit each, from −125 dBm: it is full at −29 dBm,
which only a transmitter very close by reaches. A 5 W handheld 100 m away reads about −40 to −55 dBm,
in the amber.

### Keys

| Key | Short press | Long press (0.6 s) |
|---|---|---|
| 0–9 | type a frequency or channel number | 1 TX power high/low · 2 squelch level · 3 tuning step · 4 search · 5 **Messages** · 6 channel scan · 7 **Tunes** · 8 **Pager** · 9 **Morse** |
| MENU | confirm what was typed, otherwise open the menu | switch the selected slot between channel and VFO |
| FM | FM, AM, SSB (VFO slot) | key lock on / off |
| A/B | select the other slot | |
| UP / DOWN | next / previous channel, or VFO up / down one step (repeats when held) | |
| BACK | cancel typing, stop a scan or search | |
| Side key 1 | what the menu's **Side 1** says (stock's PF3) | |
| Side key 2 | what **Side 2** says (stock's PF2); the channel scan by default | **Side 2 long** |
| Top key | what **Top key** says | |
| 0–9 while transmitting | DTMF while held | |
| PTT | transmit, from any screen, also with the keys locked or the display off | |

MENU and FM act when released, so their long presses don't also open the menu or switch FM/AM.
The first key press after the backlight went off only switches it on.

**Typing a frequency:** VFO slot, up to seven digits, MHz first: `4 7 6 6 7 5 0` is 476.6750 MHz.
Fewer digits and MENU fill the rest with zeros (`1 4 6` MENU is 146.0000 MHz). Below 100 MHz,
start with 0 (`0 2 7` MENU). On a channel slot, type a channel number (up to three digits). The
VFO tunes 18–1300 MHz, but the BK4819 does not receive or transmit in 660–840 MHz.

**Search** (long 4) runs the BK4819's frequency counter. It finds the strongest carrier nearby and
puts it in the VFO.

**Frequency scanner** (menu, 5 Scan): type **From** and **To** in MHz, the same way as a
VFO frequency (MENU moves on), choose the **Step** and the **Mode** with UP / DOWN, and MENU starts
it. It remembers all of them. It runs on the main screen, shows `SCAN`, and stops on a signal; **Scan resume** in the menu
says when it moves on (by default 2 s after the signal ends). UP and DOWN set the scan's direction
and move on at once, also off a signal it stopped on. 0 locks a frequency out for this scan. BACK stops it and leaves the slot in VFO mode on that frequency, in the mode it was scanning in. Mode
**Auto** receives airband (108–137 MHz) and 27 MHz CB (26.96–27.41 MHz) in AM and everything else in FM;
**FM** and **AM** use that mode across the whole range. It doesn't scan in SSB. It jumps over 660–840 MHz,
which the chip can't tune.

**Channel scan** (long 6 or side key 2) steps through the channels marked for scanning. It stops on
a signal and moves on 2 s after it ends. During a scan, 0 locks the current channel out for this
scan, UP and DOWN set the direction and move on at once, and BACK stops the scan on the channel it is
on.

**Squelch:** level 1 opens on weak stations that are still readable, which stock's level 1 kept
out; about level 3 is stock's level 1. Each level above needs a cleaner, stronger signal, up to
level 9 for strong stations only. Level 0 is always open. The levels start from the radio's own
calibration. The event log shows the noise reading of each signal, and a `WEAK` entry for one the
squelch kept out (1.23+).

### Transmitting

Hold PTT. You get:
- the red LED, a red TX chip on the slot, and the TX power on the meter;
- a timer on the hint line, and the time-out ("TX timeout" in the menu). At the time-out the
  transmitter stops until PTT is released.

When PTT is released, the radio can send **Roger** (a beep or a short data burst), then the **Tail
cut**: 55 Hz for a moment, so receivers that know it mute before the squelch closes on noise. A side
key set to **Monitor** sends the **Tone burst** while transmitting. With **PTT-ID** on a channel,
its DTMF code goes out at key-up, key-down or both.

Transmit happens on the **selected** slot's channel as programmed:
- its transmit frequency (a repeater shift or split);
- its CTCSS / DCS tone, scrambler, bandwidth and power;
- a VFO slot transmits on its own frequency.

PTT is refused, with a message, in these cases:
- "TX off on this channel": the channel is set to transmit-off;
- "Channel busy": the channel has busy lockout and a signal is being received;
- "TX not allowed here": a blocked range (108–138 MHz, 406.0–406.1 MHz, or 660–840 MHz where the chip
  can't synthesise).

The marks on a slot show its set-up:
- `+` / `−`: repeater shift;
- `X`: transmit-off;
- `N` / `W` / `AM`: bandwidth;
- `H` / `L`: power;
- `T`, `D` or `E`: a CTCSS or DCS tone, or stock's ENCRY code (a private code squelch: other radios
  stay quiet, but the voice itself isn't scrambled);
- `S` / `R`: a fixed or rolling voice scrambler.

While transmitting, the slot shows the transmit frequency.

**Tone squelch:** a channel with a receive tone stays silent until that tone is heard. Scan and dual
watch stop on any carrier, but move on after half a second if it doesn't carry the channel's tone.

### Scrambler

A channel's scramble setting (set in Talkpod Manager) is one of these:
- **2600–3500 Hz:** the voice is turned upside down around that frequency. These are the UV-K5's
  scrambler types 1–10, so a UV-K5 set to the same type hears you clearly. It is weak: words can
  still be made out by ear.
- **Rolling:** the same inversion, but its frequency keeps gliding between 2600 and 3500 Hz, along a
  path worked out from the **Scramble key** (menu). That is much harder to follow by ear.
  - At PTT the radio sends a short data burst (a chirp, about 0.2 s) and then your voice. Start
    talking after a moment: the first ~0.4 s after PTT isn't sent.
  - Only radios running this firmware, with the same key, can follow it. They must have heard the
    burst, so a radio that tunes in mid-transmission hears garble until the next one.
  - PTT is refused ("Set Scramble key") until a key is set.
  - A rolling channel listens for the burst instead of text messages.

Both are privacy against casual listening, not security.

## Menu

MENU opens a list of categories. UP / DOWN and MENU open one, or press its digit (1–7). In a
category, UP / DOWN choose an item, MENU starts or ends changing it (UP / DOWN change the value), and
BACK goes back to the list, then leaves. Settings are saved 3 s after the last change and survive
power-off. They're stock's own settings where stock has the same one. Talkpod Manager shows them on its
Settings page: Read brings in what the radio has, Write sends your changes. Side key functions DTMF and
Remote aren't there and say so.

**On the Messages, Pager and Morse screens, hold A/B** for that screen's own settings (the same items as
in Data modes); BACK returns to the screen.

**1 Radio**

| Item | Values |
|---|---|
| Squelch | 0–9 |
| Step | 2.5, 5, 6.25, 10, 12.5, 25, 100 kHz, 1 MHz, then 1 kHz, 100 Hz, 10 Hz for SSB |
| Bandwidth | wide / narrow (VFO slot; a channel has its own) |
| Modulation | FM, AM, SSB (VFO slot) |
| Dual watch | on / off |
| TX slot | with dual watch: Selected, or always slot A or B |
| TX timeout | off, 30 s, 60 s, 2, 4, 8 min |

**2 Transmit**

| Item | Values |
|---|---|
| Roger | off, Beep, Tone1200 |
| Tail cut | sends the squelch tail, and mutes on one received |
| Rpt tail cut, Rpt tail wait | keep the speaker muted this long after transmitting, for a repeater's tail |
| Tone burst | 1000, 1450, 1750, 2100 Hz |
| VOX | off, 1–10 (the mic level that keys the transmitter; higher needs more) |
| VOX delay | how long it keeps transmitting after you stop, 0.5–2 s |
| PTT-ID delay | wait before the PTT-ID at key-up |

**3 Screen & sound**

| Item | Values |
|---|---|
| Backlight | always on, 5, 10, 15, 20, 30 s, 1, 2, 3 min |
| Slot A shows, Slot B shows | a channel's name, frequency or number, large |
| Power-on | Logo (the boot splash) or Voltage |
| Key beep | on / off |
| Voice | on / off (power-on sound, then the mode spoken after it; channel/frequency mode, high/low) |
| Menu exit | leaves the menu after this long without a key |
| Tunes | opens the Tunes screen (long 7 on the main screen too) |

**4 Keys**

| Item | Values |
|---|---|
| Side 1, Side 2, Side 2 long, Top key | FM radio, TX power, Scan, Search, Monitor, PTT B, SOS, DTMF, Reverse, Remote, or Off |
| Auto lock | locks the keys after 5, 10, 15 s |

**5 Scan**

| Item | Values |
|---|---|
| Freq scanner | scans a frequency range: see above |
| Scan channels | starts the channel scan |
| Scan resume | Time (moves on after 5 s), Carrier (2 s after the signal ends), Stop (stays) |

**6 Data modes**

| Item | Values |
|---|---|
| Messages | opens the Messages screen (shows how many are new) |
| Msg speed | 1200 (default), 700, 450 bit/s for text messages; both radios must match |
| Msg receipts | on: answer received messages with a delivery receipt |
| Pager | opens the Pager screen |
| Pager RIC | the pager address the Pager screen sends to (0–2097151) |
| Pager speed | 512, 1200, 2400 bit/s |
| Pager invert | send and receive pages the other way round from standard POCSAG (for a sender or receiver that uses the opposite polarity); off for pagers, multimon-ng, PDW |
| Morse | opens the Morse screen |
| Morse speed | 5–40 WPM (also UP / DOWN on the Morse screen), until power-off |

**7 Other**

| Item | Values |
|---|---|
| Battery save | off, Normal, Super, Deep: 3 minutes after the last activity the receiver sleeps and listens in turns |
| Alarm | what a side key set to SOS does: Local (speaker), Tone (on the air), Code (on the air, with the ANI code) |
| Alarm sound | the Local alarm's sound with Tone / Code too, when not transmitting |
| Scramble key | the key Rolling channels use (up to 14 characters, the same on every radio) |

**About**: firmware version, board, battery, number of channels.

## FM radio

A side key set to **FM radio** opens it (65.0–108.0 MHz). UP / DOWN tune by 0.1 MHz, or type the
frequency (`1 0 1 5` is 101.5 MHz; `8 7 5` and MENU is 87.5). BACK goes back to the radio, and PTT
transmits at once. Calls aren't heard while it plays.

## SSB

On a VFO slot, FM steps through FM, AM and **SSB**. SSB is receive only: the BK4819 only transmits FM,
so PTT says so. The speaker stays open, since SSB has no carrier for a squelch to open on. Use the fine
steps (menu Step: 1k, 100 Hz, 10 Hz) and tune for natural-sounding voice, with the VFO on the signal's
suppressed carrier. One mode is meant to cover upper and lower sideband: see
[hardware.md](hardware.md#ssb-receive).

## Typing text

The Messages, Pager and Morse screens and the text editors type like an old phone keypad:

| Key | Characters (press again within a second for the next) |
|---|---|
| 1 | `. , ? ! - ' / @ 1` |
| 2–9 | the letters on the key, then the digit (`A B C 2` ...) |
| 0 | space, then `0` |
| any digit, held | the digit itself |
| FM, tapped | ABC / abc |
| FM, held | the texts you sent before (below) |
| BACK | delete the last character (hold: delete all) |

The letter that is still cycling is shown highlighted.

**Sending a text again:** on the Messages, Pager and Morse screens, hold **FM**. The typing box
shows the last text you sent, highlighted, with its number and the screen it went from ("1/12 Morse").
Each tap of **FM** goes to an older one. **MENU** puts the one shown in the typing box, to change or
send; **BACK** goes back to what you were typing, unchanged. The three screens share the list, so a
text sent as a message can go out again in Morse. The radio keeps at least the last 25, through
power-off.

**How long a text can be:**

| Screen | Characters | Why |
|---|---|---|
| Messages | 29 | the UV-K5 messenger's packet has room for 29 |
| Pager | 91 | a page goes out as one BK4819 data packet (255 bytes at most): 91 fit whatever the RIC |
| Morse | 144 | nothing limits it but the TX timeout |

A text recalled on a screen with a lower limit is cut to it ("cut to fit").

## Messages (long 5)

Text messages in the format of the UV-K5 "messenger" (kamilsss655's UV-K5 firmware), so these
radios can chat with each other and with UV-K5s running that firmware. They are sent on the
selected slot's frequency.

- History above: `<` received, `>` sent, a green `+` when the other radio confirmed delivery.
  UP / DOWN scroll.
- Type below and press MENU to send. BACK on an empty message leaves the screen.
- About 0.7 s after a message arrives, the radio answers with a short delivery receipt (menu
  "Msg receipts"). Receipts are transmissions too and obey the blocked ranges.
- A message arriving while you are elsewhere shows on the hint line, puts an envelope in the
  status bar and plays a sound. The modem tone itself is muted.
- Encrypted UV-K5 messages show as "(encrypted)".
- Hold **A/B** for the message settings: speed and receipts.

The receiver listens for messages on every screen except the Pager, which uses the modem for its
own format, and on every channel except Rolling ones (they listen for the scrambler's burst).

## Morse (long 9)

Type a text as on the Messages screen; MENU sends it in Morse code on the selected slot, as a 700 Hz
tone on the FM transmitter (MCW), so any FM radio on the channel hears it. The channel's CTCSS / DCS
goes with it, so repeaters let it through.

- **Speed:** 30 words per minute to start with, about a professional operator's speed: a dit is
  40 ms. UP and DOWN change it from 5 to 40 WPM (so do **Morse speed** in menu 6 Data modes, and holding
  **A/B**), until the radio is switched off.
- **Timing:** standard (PARIS): a dah is three dits, one dit between the elements of a letter, three
  between letters, seven between words. The carrier comes up half a second before the first letter,
  for receivers' squelch and repeaters.
- **Letters:** A–Z, 0–9 and `. , ? ' ! / ( ) & : ; = + - _ " @`; anything else is skipped. Up to
  144 characters; the typing box scrolls.
- The exact signal (tone, timing, what comes before and after) is in [morse.md](morse.md), for
  writing a decoder.
- While it sends, the letter going out is highlighted. **BACK** stops it at once; **PTT** stops it and
  transmits your voice.
- **Roger** and **Tail cut** are for voice: Morse ends without them. A message that would run past
  the **TX timeout** isn't sent; the radio says how many seconds it needs.

## Tunes (long 7)

Short tunes for fun, from a "ta-da" to about 14 seconds: Keyboard jam (melody and bass together, on the
chip's two tone generators), Ta-da!, Level up, Charge!, Shave & haircut, Sad trombone, Boing, Ode to joy,
Fur Elise, Happy birthday and Twinkle twinkle (the user's own, public domain, or made up for this
firmware). Each shows how long it is.

- UP / DOWN pick one. **MENU** plays it on the speaker (unplug the programming cable: it cuts the speaker).
- **Hold MENU** to send it on the selected slot: the tone goes out on the FM carrier the way Morse does,
  with the channel's CTCSS / DCS, the carrier coming up 0.4 s first, and no Roger or tail. A tune longer
  than the TX timeout isn't sent.
- **BACK** (or PTT) stops it.

Music isn't allowed on the amateur bands in most countries; send tunes only where your licence allows.

## Pager (long 8)

POCSAG at the menu's **Pager speed**.

- Received pages list the RIC and the text (numeric or alphanumeric); single bit errors are
  corrected.
- Type a page and press MENU: it goes to **Pager RIC** (hold A/B, or menu 6 Data modes), or to the RIC of the page picked
  with UP. All digits (and space, `-`, `U`, `(`, `)`) are sent as a numeric page, anything else as
  alphanumeric. MENU with nothing typed sends a tone-only page.

Hold **A/B** for the pager settings: RIC, speed and invert. Pager listening runs only while this screen
is open, and dual watch stays on the selected slot meanwhile.

## Boot splash

An animation plays once when the radio is switched on; any key skips it. Change it from a PC with
`tools/splash.py` (or Talkpod Manager): see
[serial-interface.md](serial-interface.md#boot-splash).

## When something goes wrong

The radio keeps a log of its last 128 receiver events in memory: the squelch opening and closing,
tunes, what muted the audio and why, data packets, transmissions. After something odd happens, plug
in the programming cable without switching off and open **Event log** in Talkpod Manager (0.5.0 or
later): it reads the log and explains each line. Switching off clears it.

## Sounds

If the sound pack is installed (`tools/soundpack.py`), the radio plays:
- power-on;
- a key beep, menu open, item open and cancel;
- "channel mode" / "frequency mode";
- "high" / "low" for TX power;
- "low voltage" every 2 minutes on the last battery bar.

No sound plays while transmitting, and received audio is held off while a sound plays.
