#!/usr/bin/env python3
"""A MIDI file's melody as a tune for src/tune.c (notes of two bytes: MIDI note | 0x80 glide, length in 10 ms).

  midi2tune.py FILE.mid info                        channels: note counts and ranges
  midi2tune.py FILE.mid tune --melody 12 [--second 5] [--seconds 20] [--speed 1.25] ...
                                                    the C arrays: the highest note sounding on those channels
                                                    (1-16; 1@0-2,11@2-10 limits a channel to notes starting in
                                                    a span, in s), for a second voice too
  midi2tune.py FILE.mid wav --melody 12 [--second 5] ... --out OUT.wav   how it will sound (sine, as the radio)

Only note and tempo events are read: text, names and other meta events are skipped without being decoded.
"""

import struct
import sys
import wave


def read_events(path):
    """[(time in s, channel 0-15, note, on)] from all tracks, with the tempo map applied."""
    data = open(path, "rb").read()
    if data[:4] != b"MThd":
        raise SystemExit("not a MIDI file")
    _fmt, ntrk, div = struct.unpack(">HHH", data[8:14])
    if div & 0x8000:
        raise SystemExit("SMPTE time isn't supported")
    pos, raw, tempos = 8 + struct.unpack(">I", data[4:8])[0], [], [(0, 500000)]
    for _ in range(ntrk):
        if data[pos:pos + 4] != b"MTrk":
            break
        end = pos + 8 + struct.unpack(">I", data[pos + 4:pos + 8])[0]
        p, tick, status = pos + 8, 0, 0
        while p < end:
            delta = 0
            while True:
                b = data[p]; p += 1
                delta = delta << 7 | b & 0x7F
                if not b & 0x80:
                    break
            tick += delta
            if data[p] & 0x80:
                status = data[p]; p += 1
            if status == 0xFF:                      # meta: only the tempo is used, the rest skipped unread
                kind = data[p]; p += 1
                n = 0
                while True:
                    b = data[p]; p += 1
                    n = n << 7 | b & 0x7F
                    if not b & 0x80:
                        break
                if kind == 0x51 and n == 3:
                    tempos.append((tick, int.from_bytes(data[p:p + 3], "big")))
                p += n
            elif status in (0xF0, 0xF7):            # sysex: skipped
                n = 0
                while True:
                    b = data[p]; p += 1
                    n = n << 7 | b & 0x7F
                    if not b & 0x80:
                        break
                p += n
            else:
                kind, ch = status & 0xF0, status & 0x0F
                nbytes = 1 if kind in (0xC0, 0xD0) else 2
                d = data[p:p + nbytes]; p += nbytes
                if kind == 0x90 and d[1] > 0:
                    raw.append((tick, ch, d[0], True))
                elif kind == 0x80 or (kind == 0x90 and d[1] == 0):
                    raw.append((tick, ch, d[0], False))
        pos = end
    tempos.sort()

    def seconds(t):
        s, last_tick, us = 0.0, 0, 500000
        for tt, tu in tempos:
            if tt >= t:
                break
            s += (tt - last_tick) * us / div / 1e6
            last_tick, us = tt, tu
        return s + (t - last_tick) * us / div / 1e6

    return sorted((seconds(t), ch, n, on) for t, ch, n, on in raw)


def notes(events, channels):
    """[(start s, end s, note)] on those channels (0-based)."""
    out, sounding = [], {}
    for t, ch, n, on in events:
        if ch not in channels:
            continue
        if on:
            sounding.setdefault((ch, n), []).append(t)
        elif sounding.get((ch, n)):
            out.append((sounding[(ch, n)].pop(0), t, n))
    return sorted(out)


def skyline(ns, start, length):
    """The highest note sounding at each moment, from start for length s: [(note or 0, ms)] in 10 ms steps."""
    steps = int(length * 100)
    grid = [(int((s - start) * 100 + 0.5), int((e - start) * 100 + 0.5), n) for s, e, n in ns]   # in 10 ms steps
    top = []
    for k in range(steps):
        live = [n for ks, ke, n in grid if ks <= k < ke]
        top.append(max(live) if live else 0)
    # a new note starts when the top note changes, or the same note is struck again
    starts = {ks for ks, ke, n in grid if 0 <= ks < steps}
    out = []
    for k, n in enumerate(top):
        if out and out[-1][0] == n and k not in starts:
            out[-1][1] += 1
        else:
            out.append([n, 1])
    while out and out[-1][0] == 0:
        out.pop()
    merged = []
    for n, c in out:                               # rests shorter than 30 ms join the note before
        if n == 0 and c < 3 and merged:
            merged[-1][1] += c
        else:
            merged.append([n, c])
    split = []
    for n, c in merged:                            # the length is one byte of 10 ms
        while c > 255:
            split.append([n, 255])
            c -= 255
        split.append([n, c])
    return split


def pick(ev, spec, first):
    """Notes of a spec like 12 or 1@0-2,11@2-10 (channel 1-16, optionally only notes starting in a span)."""
    out = []
    for c in spec.split(","):
        ch, _, span = c.partition("@")
        lo, _, hi = span.partition("-")
        lo, hi = (float(lo), float(hi)) if span else (-1e9, 1e9)
        out += [nt for nt in notes(ev, {int(ch) - 1}) if lo <= nt[0] - first < hi]
    return sorted(out)


def voice(ev, spec, start, seconds, shift, speed, first):
    ns = pick(ev, spec, first)
    t0 = (ns[0][0] if start is None else first + start)
    ns = [(t0 + (s - t0) / speed, t0 + (e - t0) / speed, n) for s, e, n in ns]   # faster around the start
    return [(n + shift if n else 0, c) for n, c in skyline(ns, t0, seconds)], t0 - first


def render(voices, path, rate=22050):
    import math
    total = max(sum(c for _, c in v) for v in voices) * rate // 100
    mix = [0.0] * total
    for v in voices:
        pos, phase = 0, 0.0
        for n, c in v:
            ms = c * 10
            gap = 20 if ms > 40 else ms // 2                 # as src/tune.c
            f = 440 * 2 ** ((n - 69) / 12) if n else 0
            on_samples = int(rate * (ms - gap) / 1000)
            for i in range(int(rate * ms / 1000)):
                phase += 2 * math.pi * f / rate
                if n and i < on_samples and pos + i < total:
                    mix[pos + i] += math.sin(phase)
            pos += int(rate * ms / 1000)
    scale = 12000 / len(voices)
    w = wave.open(path, "wb")
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(rate)
    w.writeframes(struct.pack(f"<{total}h", *(int(x * scale) for x in mix)))
    w.close()


def c_array(v):
    flat = [x for n, c in v for x in (n, c)]
    lines = [f"/* {len(v)} notes, {sum(c for _, c in v) / 100:.2f} s, MIDI {min(n for n, _ in v if n)}-{max(n for n, _ in v)} */"]
    lines += ["    " + ", ".join(str(x) for x in flat[i:i + 20]) + "," for i in range(0, len(flat), 20)]
    return "\n".join(lines)


def main():
    import argparse
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("file")
    ap.add_argument("cmd", choices=["info", "tune", "wav"])
    ap.add_argument("--melody", help="channels, e.g. 12 or 1@0-2,11@2-10")
    ap.add_argument("--second", help="a second voice's channels (the radio's second tone generator)")
    ap.add_argument("--seconds", type=float, default=10, help="length of the tune, after --speed")
    ap.add_argument("--start", type=float, help="s into the song (default: the melody's first note)")
    ap.add_argument("--shift", type=int, default=0, help="semitones, melody")
    ap.add_argument("--second-shift", type=int, default=0, help="semitones, second voice")
    ap.add_argument("--speed", type=float, default=1.0, help="1.25: a quarter faster")
    ap.add_argument("--out", help="the WAV to write (wav)")
    a = ap.parse_args()
    ev = read_events(a.file)
    first = min((t for t, ch, n, on in ev if on), default=0)
    if a.cmd == "info":
        for ch in sorted({c for _, c, _, _ in ev}):
            ns = notes(ev, {ch})
            if ns:
                print(f"channel {ch + 1:2d}: {len(ns):4d} notes, MIDI {min(n for _, _, n in ns)}-"
                      f"{max(n for _, _, n in ns)}, from {ns[0][0] - first:6.2f} s to {max(e for _, e, _ in ns) - first:6.2f} s")
        return
    mel, at = voice(ev, a.melody, a.start, a.seconds, a.shift, a.speed, first)
    voices = [mel]
    if a.second:
        voices.append(voice(ev, a.second, at, a.seconds, a.second_shift, a.speed, first)[0])
    if a.cmd == "tune":
        for k, v in enumerate(voices):
            print(f"/* voice {k + 1}, from {at:.2f} s at speed {a.speed} */")
            print(c_array(v))
    else:
        render(voices, a.out)
        print(f"{a.out}: {sum(c for _, c in mel) / 100:.2f} s, {len(voices)} voice(s), from {at:.2f} s, speed {a.speed}")


if __name__ == "__main__":
    main()
