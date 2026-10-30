#!/usr/bin/env python3
"""Boot splash for Talkpod Super Firmware: convert an animated GIF and write it to the radio.

Command line:
  splash.py GIF [--port /dev/ttyUSB0] [--bg 000000] [--max-ms 15000] [--preview DIR]
  splash.py GIF --image FLASH_IN FLASH_OUT      write into a flash image (for tools/emu.py)
  splash.py --remove                            erase the splash (the radio then boots without one)
  splash.py --play                              play the splash on the radio now

As a module (self-contained: needs only Pillow and pyserial), e.g. from Talkpod Manager:

  import splash
  blob, info = splash.convert("logo.gif")               # bytes in the A36G format, details
  with splash.Radio("/dev/ttyUSB0") as r:
      print(r.info())                                   # {'fw': 'TSF', 'ver': '1.00', ...}
      r.upload(blob, progress=lambda done, total: ...)  # erase, write, verify
      r.play()                                          # preview it

The format and the serial commands are described in docs/serial-interface.md. In short: at
0x120000 (768 KB to 0x1DFFFF), little-endian,

  "A36G", u16 frames, u16 background RGB565, u32 total bytes
  per frame: u16 delay ms, u8 x, y, w, h (w = 0: nothing changes), u16 colours, colours x u16
             RGB565, u32 data bytes, data: PackBits over palette indices of the w x h rectangle,
             row by row (n < 128: n + 1 literal indices follow; n >= 128: the next index n - 125
             times)

Frames are fitted into 128 x 128 (centred on the background colour) and stored as the rectangle
that changed since the previous frame. The radio plays the animation once at power-on; any key
skips it.
"""
import argparse
import os
import struct
import sys
import time

BASE, END = 0x120000, 0x1E0000          # the splash area: 768 KB
SIZE = 128
SECTOR = 0x1000


def rgb565(r, g, b):
    return (r & 0xF8) << 8 | (g & 0xFC) << 3 | b >> 3


def crc16(data, crc=0):
    """CRC-16/XMODEM, as the radio's 'W' and 'X' commands use"""
    for x in data:
        crc ^= x << 8
        for _ in range(8):
            crc = (crc << 1 ^ 0x1021 if crc & 0x8000 else crc << 1) & 0xFFFF
    return crc


# ---- converting -------------------------------------------------------------------------------

def _frames(gif, bg, max_ms):
    from PIL import Image, ImageSequence

    def pixels(im):
        return getattr(im, "get_flattened_data", im.getdata)()

    im = Image.open(gif)
    out, total = [], 0
    for fr in ImageSequence.Iterator(im):
        delay = max(20, int(fr.info.get("duration", 100) or 100))
        rgba = fr.convert("RGBA")
        scale = min(SIZE / rgba.width, SIZE / rgba.height)
        w, h = max(1, round(rgba.width * scale)), max(1, round(rgba.height * scale))
        rgba = rgba.resize((w, h), Image.LANCZOS)
        canvas = Image.new("RGBA", (SIZE, SIZE), tuple(bg) + (255,))
        canvas.alpha_composite(rgba, ((SIZE - w) // 2, (SIZE - h) // 2))
        canvas = canvas.convert("RGB")
        if len(set(pixels(canvas))) > 256:
            canvas = canvas.quantize(256, dither=Image.Dither.NONE).convert("RGB")
        out.append(([rgb565(*p) for p in pixels(canvas)], delay, canvas))
        total += delay
        if total >= max_ms:
            break
    return out


def _packbits(idx):
    out, i, n = bytearray(), 0, len(idx)
    while i < n:
        run = 1
        while i + run < n and idx[i + run] == idx[i] and run < 130:
            run += 1
        if run >= 3:
            out += bytes([run + 125, idx[i]])
            i += run
            continue
        j = i
        while j < n and j - i < 128:
            if j + 2 < n and idx[j] == idx[j + 1] == idx[j + 2]:
                break
            j += 1
        out += bytes([j - i - 1]) + bytes(idx[i:j])
        i = j
    return bytes(out)


def _build(frames, bg565):
    body, prev = bytearray(), None
    for px, delay, _ in frames:
        if prev is None:
            x0, y0, x1, y1 = 0, 0, SIZE - 1, SIZE - 1
        else:
            changed = [i for i in range(SIZE * SIZE) if px[i] != prev[i]]
            if not changed:
                body += struct.pack("<HBBBBHI", delay, 0, 0, 0, 0, 0, 0)
                continue
            xs, ys = [i % SIZE for i in changed], [i // SIZE for i in changed]
            x0, y0, x1, y1 = min(xs), min(ys), max(xs), max(ys)
        w, h = x1 - x0 + 1, y1 - y0 + 1
        region = [px[(y0 + y) * SIZE + x0 + x] for y in range(h) for x in range(w)]
        pal = sorted(set(region))
        index = {c: i for i, c in enumerate(pal)}
        data = _packbits([index[c] for c in region])
        body += struct.pack("<HBBBBH", delay, x0, y0, w, h, len(pal))
        body += b"".join(struct.pack("<H", c) for c in pal)
        body += struct.pack("<I", len(data)) + data
        prev = px
    return b"A36G" + struct.pack("<HHI", len(frames), bg565, 12 + len(body)) + bytes(body)


def convert(gif, bg=(0, 0, 0), max_ms=15000, preview_dir=None):
    """An animated (or still) image file as splash bytes, and a dict with frames, ms and bytes.
    Raises ValueError if it does not fit the splash area."""
    frames = _frames(gif, bg, max_ms)
    blob = _build(frames, rgb565(*bg))
    info = {"frames": len(frames), "ms": sum(d for _, d, _ in frames), "bytes": len(blob),
            "capacity": END - BASE}
    if preview_dir:
        os.makedirs(preview_dir, exist_ok=True)
        for i, (_, _, img) in enumerate(frames):
            img.save(os.path.join(preview_dir, f"frame{i:03d}.png"))
    if len(blob) > END - BASE:
        raise ValueError(f"{len(blob)} bytes: more than the {END - BASE} the splash area holds; "
                         "use a shorter GIF, fewer colours or a smaller max_ms")
    return blob, info


# ---- the radio's serial shell -----------------------------------------------------------------

class RadioError(Exception):
    pass


class Radio:
    """The firmware's serial shell (57600 8N1, text commands ending in CR, replies ending in CRLF).
    The radio may also print log lines (keys pressed, messages heard); replies are picked out by
    their content."""

    def __init__(self, port="/dev/ttyUSB0"):
        import serial
        self.s = serial.Serial(port, 57600, timeout=0.2)
        self.s.write(b"\r")
        time.sleep(0.3)
        self.s.read(65536)

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def close(self):
        self.s.close()

    def _line(self, cmd, want, wait=3.0):
        """sends cmd; the first reply line that want(line) accepts, or None"""
        self.s.reset_input_buffer()
        self.s.write(cmd if isinstance(cmd, bytes) else (cmd + "\r").encode())
        t0, buf = time.time(), b""
        while time.time() - t0 < wait:
            buf += self.s.read(max(1, self.s.in_waiting))     # what has arrived, without waiting
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                line = line.decode("latin-1").strip()
                if want(line):
                    return line
        return None

    def info(self):
        """{'fw': 'TSF', 'ver': '1.00', 'board': ..., 'splash': '00120000+000C0000', ...}; RadioError
        if the radio does not answer (not running this firmware)"""
        line = self._line("i", lambda l: l.startswith("fw="))
        if not line:
            raise RadioError("no answer to 'i': is the radio on and running Talkpod Super Firmware?")
        return dict(kv.split("=", 1) for kv in line.split())

    def erase(self, addr):
        if self._line(f"E {addr:X}", lambda l: l in ("ok", "refused")) != "ok":
            raise RadioError(f"erasing 0x{addr:06X} failed")

    def write(self, addr, data):
        """up to 256 bytes into erased flash"""
        for _ in range(3):
            self.s.reset_input_buffer()
            self.s.write(f"W {addr:X} {len(data)}\r".encode())
            time.sleep(0.05)
            line = self._line(data + struct.pack(">H", crc16(data)),
                              lambda l: l in ("ok", "crc", "refused", "timeout"))
            if line == "ok":
                return
        raise RadioError(f"writing 0x{addr:06X} failed")

    def read(self, addr, n):
        """n bytes of flash ('X': raw bytes, then their CRC-16/XMODEM big-endian)"""
        for _ in range(4):
            self.s.reset_input_buffer()
            self.s.write(f"X {addr:X} {n}\r".encode())
            t0, data = time.time(), b""
            while time.time() - t0 < 3 + n / 4000 and len(data) < n + 2:
                data += self.s.read(n + 2 - len(data))
            if len(data) == n + 2 and crc16(data[:n]) == data[n] << 8 | data[n + 1]:
                return data[:n]
        raise RadioError(f"reading 0x{addr:06X} failed")

    def upload(self, blob, progress=None):
        """erases what the splash needs, writes it and reads it back; progress(done, total) bytes"""
        if len(blob) > END - BASE or blob[:4] != b"A36G":
            raise RadioError("not a splash (A36G) or too big")
        total = len(blob) * 2
        for addr in range(BASE, BASE + len(blob), SECTOR):
            self.erase(addr)
        for off in range(0, len(blob), 256):
            self.write(BASE + off, blob[off:off + 256])
            if progress:
                progress(off + 256, total)
        for off in range(0, len(blob), 4096):
            n = min(4096, len(blob) - off)
            if self.read(BASE + off, n) != blob[off:off + n]:
                raise RadioError(f"verify failed at 0x{BASE + off:06X}")
            if progress:
                progress(len(blob) + off + n, total)

    def remove(self):
        """no splash at the next power-on"""
        self.erase(BASE)

    def play(self):
        """plays the splash now; False if there is none"""
        return self._line("G", lambda l: l in ("ok", "no splash"), wait=30) == "ok"


# ---- command line -----------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("gif", nargs="?")
    ap.add_argument("--port", default="/dev/ttyUSB0")
    ap.add_argument("--bg", default="000000", help="background colour (hex RGB)")
    ap.add_argument("--max-ms", type=int, default=15000, help="stop the animation after this long")
    ap.add_argument("--image", nargs=2, metavar=("FLASH_IN", "FLASH_OUT"))
    ap.add_argument("--preview", metavar="DIR", help="also save the frames as PNGs")
    ap.add_argument("--remove", action="store_true")
    ap.add_argument("--play", action="store_true")
    a = ap.parse_args()

    if a.remove or (a.play and not a.gif):
        with Radio(a.port) as r:
            if a.remove:
                r.remove()
                print("splash removed")
            else:
                print("played" if r.play() else "the radio has no splash")
        return
    if not a.gif:
        ap.error("a GIF, --remove or --play")
    bg = tuple(int(a.bg[i:i + 2], 16) for i in (0, 2, 4))
    try:
        blob, info = convert(a.gif, bg, a.max_ms, a.preview)
    except ValueError as e:
        raise SystemExit(str(e))
    print(f"{info['frames']} frames, {info['ms'] / 1000:.1f} s, {info['bytes']} bytes "
          f"({info['bytes'] * 100 // info['capacity']}% of the splash area)")
    if a.image:
        img = bytearray(open(a.image[0], "rb").read())
        img[BASE:END] = b"\xff" * (END - BASE)
        img[BASE:BASE + len(blob)] = blob
        open(a.image[1], "wb").write(img)
        print(f"written into {a.image[1]}")
        return
    with Radio(a.port) as r:
        print("radio:", " ".join(f"{k}={v}" for k, v in r.info().items()))
        t0 = time.time()
        shown = [-1]

        def progress(done, total):
            if done * 100 // total != shown[0]:
                shown[0] = done * 100 // total
                print(f"\r  {shown[0]}%", end="", flush=True)
        r.upload(blob, progress)
        print(f"\rwritten and verified in {time.time() - t0:.0f} s")
        if a.play:
            r.play()


if __name__ == "__main__":
    main()
