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

### Keys

| Key | Short press | Long press (0.6 s) |
|---|---|---|
| 0–9 | type a frequency or channel number | 1 TX power high/low · 2 squelch level · 3 tuning step · 4 search · 5 **Messages** · 6 channel scan · 7 **APRS** · 8 **Pager** |
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

**Frequency scanner** (first item in the menu): type **From** and **To** in MHz, the same way as a
VFO frequency (MENU moves on), choose the **Step** with UP / DOWN, and MENU starts it. It remembers
the range. It runs on the main screen, shows `SCAN`, and stops on a signal; **Scan resume** in the menu
says when it moves on (by default 2 s after the signal ends). UP and DOWN set the scan's direction
and move on at once, also off a signal it stopped on. 0 locks a frequency out for this scan. BACK stops it and leaves the slot in VFO mode on that frequency. Airband (108–137 MHz)
and 27 MHz CB (26.96–27.41 MHz) are received in AM automatically, and it jumps over 660–840 MHz, which
the chip can't tune.

**Channel scan** (long 6 or side key 2) steps through the channels marked for scanning. It stops on
a signal and moves on 2 s after it ends. During a scan, 0 locks the current channel out for this
scan, UP and DOWN set the direction and move on at once, and BACK stops the scan on the channel it is
on.

**Squelch** levels 1–9 use the radio's own calibration. Level 1 in narrow is stock's level 1.
Wide FM is 15 points less sensitive to noise than stock, because stock's wide squelch opened on
static. Each level above 1 needs a stronger signal. Level 0 is always open.

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

UP / DOWN choose an item, MENU starts or ends changing it (UP / DOWN change the value), BACK
leaves. Settings are saved 3 s after the last change and survive power-off. They're stock's own settings where
stock has the same one. Talkpod Manager shows them on its Settings page: Read brings in what the radio
has, Write sends your changes. Side key functions DTMF and Remote aren't there and say so.

| Item | Values |
|---|---|
| Freq scanner | scans a frequency range: see above |
| Messages | opens the Messages screen (shows how many are new) |
| APRS | opens the APRS screen |
| Pager | opens the Pager screen |
| Squelch | 0–9 |
| Step | 2.5, 5, 6.25, 10, 12.5, 25, 100 kHz, 1 MHz, then 1 kHz, 100 Hz, 10 Hz for SSB |
| Bandwidth | wide / narrow (VFO slot; a channel has its own) |
| Modulation | FM, AM, SSB (VFO slot) |
| Dual watch | on / off |
| Backlight | always on, 5, 10, 15, 20, 30 s, 1, 2, 3 min |
| TX timeout | off, 30 s, 60 s, 2, 4, 8 min |
| Msg speed | 1200 (default), 700, 450 bit/s for text messages; both radios must match |
| Msg receipts | on: answer received messages with a delivery receipt |
| APRS call | your callsign with SSID, e.g. VK2ABC-7 (an editor opens) |
| APRS path | none, WIDE1-1, WIDE2-1, WIDE1-1 + WIDE2-1 |
| APRS beacon | the status text MENU sends on the APRS screen |
| Pager RIC | the pager address the Pager screen sends to (0–2097151) |
| Pager speed | 512, 1200, 2400 bit/s |
| Pager invert | send the pager FSK inverted (in case a receiver expects the other polarity) |
| Scramble key | the key Rolling channels use (up to 14 characters, the same on every radio) |
| Key beep | on / off |
| Voice | on / off (power-on sound, then the mode spoken after it; channel/frequency mode, high/low) |
| Scan channels | starts the channel scan |
| Battery save | off, Normal, Super, Deep: 3 minutes after the last activity the receiver sleeps and listens in turns |
| VOX | off, 1–10 (the mic level that keys the transmitter; higher needs more) |
| VOX delay | how long it keeps transmitting after you stop, 0.5–2 s |
| PTT-ID delay | wait before the PTT-ID at key-up |
| Alarm | what a side key set to SOS does: Local (speaker), Tone (on the air), Code (on the air, with the ANI code) |
| Alarm sound | the Local alarm's sound with Tone / Code too, when not transmitting |
| Scan resume | Time (moves on after 5 s), Carrier (2 s after the signal ends), Stop (stays) |
| Slot A shows, Slot B shows | a channel's name, frequency or number, large |
| Auto lock | locks the keys after 5, 10, 15 s |
| TX slot | with dual watch: Selected, or always slot A or B |
| Roger | off, Beep, Tone1200 |
| Tail cut | sends the squelch tail, and mutes on one received |
| Rpt tail cut, Rpt tail wait | keep the speaker muted this long after transmitting, for a repeater's tail |
| Tone burst | 1000, 1450, 1750, 2100 Hz |
| Power-on | Logo (the boot splash) or Voltage |
| Menu exit | leaves the menu after this long without a key |
| Side 2, Side 2 long, Side 1, Top key | FM radio, TX power, Scan, Search, Monitor, PTT B, SOS, DTMF, Reverse, Remote, or Off |
| About | firmware version, board, battery, number of channels |

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

The Messages, APRS and Pager screens and the text editors type like an old phone keypad:

| Key | Characters (press again within a second for the next) |
|---|---|
| 1 | `. , ? ! - ' / @ 1` |
| 2–9 | the letters on the key, then the digit (`A B C 2` ...) |
| 0 | space, then `0` |
| any digit, held | the digit itself |
| FM | ABC / abc |
| BACK | delete the last character (hold: delete all) |

The letter that is still cycling is shown highlighted. Messages are up to 29 characters.

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

The receiver listens for messages on every screen except APRS and Pager, which use the modem for
their own formats, and on every channel except Rolling ones (they listen for the scrambler's burst).

## APRS (long 7)

Set **APRS call** in the menu first; nothing is sent without it.

- MENU with nothing typed sends a status **beacon** with the menu's beacon text, for example
  `VK2ABC-7>APZA36,WIDE1-1:>A36SE CFW`.
- UP picks a heard station (highlighted). Type, then MENU sends it an APRS **message**
  (`:VK2XYZ-9 :text{12`). When its ack arrives, your message gets a green `+`.
- Messages addressed to your call are shown in amber and **acked automatically**. Repeats of the
  same message are acked again but listed once.
- Positions show as `3351.20S 15112.40E comment`; objects, Mic-E and other packets get a short
  summary.

Receiving APRS is **experimental**. See [data-modes.md](data-modes.md#aprs): the BK4819 has no
Bell 202 demodulator, so the firmware borrows its FFSK one. APRS listening runs only while this
screen is open.

## Pager (long 8)

POCSAG at the menu's **Pager speed**.

- Received pages list the RIC and the text (numeric or alphanumeric); single bit errors are
  corrected.
- Type a page and press MENU: it goes to **Pager RIC** (menu), or to the RIC of the page picked
  with UP. All digits (and space, `-`, `U`, `(`, `)`) are sent as a numeric page, anything else as
  alphanumeric. MENU with nothing typed sends a tone-only page.

Pager listening runs only while this screen is open.

## Boot splash

An animation plays once when the radio is switched on; any key skips it. Change it from a PC with
`tools/splash.py` (or Talkpod Manager): see
[serial-interface.md](serial-interface.md#boot-splash).

## Sounds

If the sound pack is installed (`tools/soundpack.py`), the radio plays:
- power-on;
- a key beep, menu open, item open and cancel;
- "channel mode" / "frequency mode";
- "high" / "low" for TX power;
- "low voltage" every 2 minutes on the last battery bar.

No sound plays while transmitting, and received audio is held off while a sound plays.
