#!/usr/bin/env python3
"""APRS test helpers for tools/emu.py.

  aprs_emu.py cfg FLASH_IN FLASH_OUT CALL [PATH [TEXT]]
      copy a flash image with an APRS settings record (area 1, 0x1E1000) in it
  aprs_emu.py rx SRC DEST INFO
      print the hex for emu.py --fsk-rx: the raw tone decisions the BK4819's demodulator would hand
      over after syncing on four flags (NRZI, stuffed, MSB first, a few more flags before the frame)
  aprs_emu.py tx LOG
      decode the AFSK the firmware sent from an emu.py --bk log: each REG_71 write is a tone change
      (an NRZI 0) at a bit boundary, the cycles between writes give the 1s in between
"""
import struct
import sys

BIT = 48000000 // 1200


def crc_xmodem(b):
    c = 0
    for x in b:
        c ^= x << 8
        for _ in range(8):
            c = (c << 1 ^ 0x1021 if c & 0x8000 else c << 1) & 0xFFFF
    return c


def fcs(b):
    c = 0xFFFF
    for x in b:
        c ^= x
        for _ in range(8):
            c = c >> 1 ^ 0x8408 if c & 1 else c >> 1
    return ~c & 0xFFFF


def addr(call, last, cbit=0):
    name, _, ssid = call.partition("-")
    return bytes((ord(c) << 1) for c in name.ljust(6)) + bytes([cbit | 0x60 | int(ssid or 0) << 1 | last])


def frame(src, dest, info, digis=()):
    a = addr(dest, 0, 0x80) + addr(src, 0 if digis else 1)
    for i, d in enumerate(digis):
        a += addr(d, i == len(digis) - 1)
    f = a + b"\x03\xf0" + info.encode()
    return f + struct.pack("<H", fcs(f))


def bits_of(f):
    out, ones = [], 0
    for x in f:
        for i in range(8):
            b = x >> i & 1
            out.append(b)
            ones = ones + 1 if b else 0
            if ones == 5:
                out.append(0)
                ones = 0
    return out


def flags(n):
    return [0, 1, 1, 1, 1, 1, 1, 0] * n


def deframe(bits):
    frames, sr, cur, nb, ones, buf = [], 0, 0, 0, 0, None
    for d in bits:
        sr = (sr >> 1 | d << 7) & 0xFF
        if sr == 0x7E:
            if buf is not None and len(buf) >= 18 and fcs(buf[:-2]) == buf[-2] | buf[-1] << 8:
                frames.append(bytes(buf[:-2]))
            buf, nb, ones = [], 0, 0
            continue
        if buf is None:
            continue
        if d:
            ones += 1
            if ones >= 7:
                buf = None
                continue
        else:
            if ones == 5:
                ones = 0
                continue
            ones = 0
        cur = (cur >> 1 | d << 7) & 0xFF
        nb += 1
        if nb == 8:
            buf.append(cur)
            nb = 0
    return frames


def show(f):
    def call(a):
        s = "".join(chr(c >> 1) for c in a[:6]).strip()
        ssid = a[6] >> 1 & 15
        return f"{s}-{ssid}" if ssid else s
    n, path = 14, []
    while not f[n - 1] & 1:
        path.append(call(f[n:n + 7]))
        n += 7
    return f"{call(f[7:14])}>{call(f[0:7])}{''.join(',' + p for p in path)}:{f[n + 2:].decode('latin-1')}"


def main():
    cmd = sys.argv[1]
    if cmd == "cfg":
        img = bytearray(open(sys.argv[2], "rb").read())
        call = sys.argv[4].encode()
        path = int(sys.argv[5]) if len(sys.argv) > 5 else 1
        text = (sys.argv[6] if len(sys.argv) > 6 else "A36SE CFW").encode()
        payload = call.ljust(10, b"\0") + bytes([path]) + text.ljust(15, b"\0")
        rec = struct.pack("<HBB", 0xA36C, len(payload), 0) + payload.ljust(26, b"\0")
        rec += struct.pack("<H", crc_xmodem(rec))
        img[0x1E1000:0x1E1000 + 0x1000] = b"\xff" * 0x1000
        img[0x1E1000:0x1E1020] = rec
        open(sys.argv[3], "wb").write(img)
    elif cmd == "rx":
        data = flags(10) + bits_of(frame(sys.argv[2], sys.argv[3], sys.argv[4])) + flags(3)
        tone, raw = 1, []
        for b in data:
            if not b:
                tone ^= 1
            raw.append(tone)
        raw += [tone] * (-len(raw) % 16)
        print(bytes(int("".join(map(str, raw[i:i + 8])), 2) for i in range(0, len(raw), 8)).hex())
    elif cmd == "tx":
        writes, end = [], None
        for line in open(sys.argv[2]):
            p = line.split()
            if len(p) >= 7 and p[2] == "BK" and p[3] == "W" and p[-1].startswith("@"):
                if p[4] == "0x71":
                    writes.append(int(p[-1][1:]))
                elif p[4] == "0x70" and p[6] == "0x0000" and writes:
                    end = int(p[-1][1:])
                    bits = []
                    for a, b in zip(writes[1:], writes[2:] + [end]):
                        k = round((b - a) / BIT)
                        bits += [0] + [1] * (k - 1)
                    t = (end - writes[1]) / BIT
                    for f in deframe(bits):
                        print(f"{show(f)}   ({len(bits)} bits, {t / 1200:.2f} s)")
                    writes = []


if __name__ == "__main__":
    main()
