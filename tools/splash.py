#!/usr/bin/env python3
"""Convert an animated GIF into the firmware's boot splash and write it to the radio.

usage: splash.py GIF [--port /dev/ttyUSB0] [--bg 000000] [--max-ms 15000]
                     [--image FLASH_IN FLASH_OUT] [--preview DIR] [--erase]

The GIF is scaled to fit 128 x 128 (centred on --bg) and played once at power-on; any key skips it.
Each frame is stored as the rectangle that changed since the previous one, with its own palette
(up to 256 RGB565 colours) and PackBits-coded palette indices. Layout at 0x120000 (little-endian):

  "A36G", u16 frames, u16 background RGB565, u32 total bytes
  per frame: u16 delay ms, u8 x, y, w, h (w = 0: nothing changes), u16 colours, colours x u16,
             u32 data bytes, data (n < 128: n + 1 literal indices follow; n >= 128: the next index
             n - 125 times)

The splash area is 0x120000-0x1DFFFF (768 KB). --image writes into a copy of a flash image instead
of the radio (for tools/emu.py); --preview saves the frames as PNGs; --erase removes the splash.
Needs the custom firmware's serial shell ('E', 'W', 'X'), Pillow and pyserial.
"""
import argparse
import os
import struct
import sys
import time

from PIL import Image, ImageSequence

BASE, END = 0x120000, 0x1E0000
SIZE = 128


def rgb565(r, g, b):
    return (r & 0xF8) << 8 | (g & 0xFC) << 3 | b >> 3


def frames_of(path, bg, max_ms):
    """the GIF's frames composited, fitted into SIZE x SIZE, as (rgb565 pixel list, delay ms)"""
    im = Image.open(path)
    out, total = [], 0
    for fr in ImageSequence.Iterator(im):
        delay = max(20, int(fr.info.get("duration", 100) or 100))
        rgb = fr.convert("RGBA")
        scale = min(SIZE / rgb.width, SIZE / rgb.height)
        w, h = max(1, round(rgb.width * scale)), max(1, round(rgb.height * scale))
        rgb = rgb.resize((w, h), Image.LANCZOS)
        canvas = Image.new("RGBA", (SIZE, SIZE), bg + (255,))
        canvas.alpha_composite(rgb, ((SIZE - w) // 2, (SIZE - h) // 2))
        canvas = canvas.convert("RGB")
        pixels = lambda im: getattr(im, "get_flattened_data", im.getdata)()
        if len(set(pixels(canvas))) > 256:
            canvas = canvas.quantize(256, dither=Image.Dither.NONE).convert("RGB")
        out.append(([rgb565(*p) for p in pixels(canvas)], delay, canvas))
        total += delay
        if total >= max_ms:
            print(f"stopped at {len(out)} frames: --max-ms {max_ms}")
            break
    return out


def packbits(idx):
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


def build(frames, bg565):
    body, prev = bytearray(), None
    for px, delay, _ in frames:
        if prev is None:
            x0, y0, x1, y1 = 0, 0, SIZE - 1, SIZE - 1
        else:
            changed = [i for i in range(SIZE * SIZE) if px[i] != prev[i]]
            if changed:
                ys = [i // SIZE for i in changed]
                xs = [i % SIZE for i in changed]
                x0, y0, x1, y1 = min(xs), min(ys), max(xs), max(ys)
        if prev is not None and not changed:
            body += struct.pack("<HBBBBH", delay, 0, 0, 0, 0, 0) + struct.pack("<I", 0)
            continue
        w, h = x1 - x0 + 1, y1 - y0 + 1
        region = [px[(y0 + y) * SIZE + x0 + x] for y in range(h) for x in range(w)]
        pal = sorted(set(region))
        index = {c: i for i, c in enumerate(pal)}
        data = packbits([index[c] for c in region])
        body += struct.pack("<HBBBBH", delay, x0, y0, w, h, len(pal))
        body += b"".join(struct.pack("<H", c) for c in pal)
        body += struct.pack("<I", len(data)) + data
        prev = px
    head = b"A36G" + struct.pack("<HHI", len(frames), bg565, 12 + len(body))
    return head + bytes(body)


def upload(blob, port):
    import serial
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from flash_backup import crc16, read_chunk

    s = serial.Serial(port, 57600, timeout=0.2)
    s.write(b"\r")
    time.sleep(0.3)
    s.read(65536)

    def command(line, wait=3.0):
        s.reset_input_buffer()
        s.write((line + "\r").encode())
        t0, reply = time.time(), b""
        while time.time() - t0 < wait and not reply.endswith(b"\n"):
            reply += s.read(64)
        return reply.decode("latin-1").strip()

    end = BASE + (len(blob) + 0xFFF & ~0xFFF)
    for addr in range(BASE, end, 0x1000):
        reply = command(f"E {addr:X}")
        if reply != "ok":
            raise SystemExit(f"erase 0x{addr:06X}: {reply!r}")
    print(f"erased 0x{BASE:06X}-0x{end - 1:06X}")
    t0 = time.time()
    for off in range(0, len(blob), 256):
        chunk = blob[off:off + 256]
        for _ in range(3):
            s.reset_input_buffer()
            s.write(f"W {BASE + off:X} {len(chunk)}\r".encode())
            time.sleep(0.05)
            s.write(chunk + struct.pack(">H", crc16(chunk)))
            t1, reply = time.time(), b""
            while time.time() - t1 < 3 and not reply.endswith(b"\n"):
                reply += s.read(64)
            if reply.strip() == b"ok":
                break
        else:
            raise SystemExit(f"write 0x{BASE + off:06X}: {reply!r}")
        if off % 16384 == 0:
            print(f"  {off * 100 // len(blob)}%  ({time.time() - t0:.0f} s)", flush=True)
    s.timeout = 3
    back = b""
    for off in range(0, len(blob), 4096):
        back += read_chunk(s, BASE + off)
    if back[:len(blob)] != blob:
        raise SystemExit("verify failed: the splash in flash differs")
    print("written and verified; it plays at the next power-on")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("gif", nargs="?")
    ap.add_argument("--port", default="/dev/ttyUSB0")
    ap.add_argument("--bg", default="000000")
    ap.add_argument("--max-ms", type=int, default=15000)
    ap.add_argument("--image", nargs=2, metavar=("FLASH_IN", "FLASH_OUT"))
    ap.add_argument("--preview")
    ap.add_argument("--erase", action="store_true", help="remove the splash (erase its first sector)")
    a = ap.parse_args()

    if a.erase:
        blob = b"\xff" * 16
    else:
        bg = tuple(int(a.bg[i:i + 2], 16) for i in (0, 2, 4))
        frames = frames_of(a.gif, bg, a.max_ms)
        blob = build(frames, rgb565(*bg))
        ms = sum(d for _, d, _ in frames)
        print(f"{len(frames)} frames, {ms / 1000:.1f} s, {len(blob)} bytes "
              f"({len(blob) * 100 // (END - BASE)}% of the splash area)")
        if len(blob) > END - BASE:
            raise SystemExit("too big: use a shorter GIF, fewer colours or --max-ms")
        if a.preview:
            os.makedirs(a.preview, exist_ok=True)
            for i, (_, _, img) in enumerate(frames):
                img.save(os.path.join(a.preview, f"frame{i:03d}.png"))
    if a.image:
        img = bytearray(open(a.image[0], "rb").read())
        img[BASE:END] = b"\xff" * (END - BASE)
        img[BASE:BASE + len(blob)] = blob
        open(a.image[1], "wb").write(img)
        print(f"written into {a.image[1]}")
    else:
        upload(blob, a.port)


if __name__ == "__main__":
    main()
