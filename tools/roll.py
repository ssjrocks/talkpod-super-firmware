#!/usr/bin/env python3
"""Rolling scrambler (src/roll.c): the reference path, and test helpers for tools/emu.py.

  roll.py cfg FLASH_IN FLASH_OUT KEY [FIRST LAST]
      copy a flash image with the scramble key (settings area 3, 0x1E3000) and channels FIRST..LAST
      (default 1..40) set to Rolling
  roll.py burst KEY IV
      the 10 bytes (as five FIFO words) for emu.py --fsk-rx: the 8-byte burst and 2 more, as received
  roll.py path KEY IV [MS]
      the inversion frequency every 10 ms for MS ms (default 500)
  roll.py check LOG KEY
      from an emu.py --bk log: the start value of the burst the firmware sent (REG_5F writes) or the
      IV given with --iv, then every REG_71 write against the path: the time offset that fits best
      and the worst error in Hz there
"""
import re
import struct
import sys

LO, HI, SLOPE = 2600, 3500, 3
FNV_START = 2166136261
M32 = 0xFFFFFFFF


def fnv(data, h=FNV_START):
    for b in data:
        h = ((h ^ b) * 16777619) & M32
    return h


class Path:
    """roll.c's path_start() / segment() / path_hz(), with C's unsigned and truncating arithmetic."""

    def __init__(self, key, iv):
        self.rng = (fnv(key.encode()) ^ (iv * 2654435761) & M32) | 1
        self.t0 = self.dur = 0
        self.fb = LO + self.rnd() % (HI - LO + 1)
        self.fa = self.fb
        self.segment()

    def rnd(self):
        x = self.rng
        x ^= (x << 13) & M32
        x ^= x >> 17
        x ^= (x << 5) & M32
        self.rng = x
        return x

    def segment(self):
        r = self.rnd()
        self.t0 += self.dur
        self.fa = self.fb
        self.fb = LO + r % (HI - LO + 1)
        self.dur = abs(self.fb - self.fa) // SLOPE + 40 + (r >> 20) % 80

    def hz(self, t):
        while t >= self.t0 + self.dur:
            self.segment()
        n = (self.fb - self.fa) * (t - self.t0)
        return self.fa + (abs(n) // self.dur) * (1 if n >= 0 else -1)


def word(hz):
    return hz * 103244 // 10000


def burst(key, iv):
    p = bytes([1]) + struct.pack("<I", iv) + bytes([fnv(key.encode()) >> 24])
    return p + struct.pack("<H", fnv(p) & 0xFFFF)


def crc_xmodem(b):
    c = 0
    for x in b:
        c ^= x << 8
        for _ in range(8):
            c = (c << 1 ^ 0x1021 if c & 0x8000 else c << 1) & 0xFFFF
    return c


LINE = re.compile(r"^\s*(\d+) ms\s+BK W 0x([0-9A-F]{2}) = 0x([0-9A-F]{4})")


def check(log, key, iv=None):
    writes = [(int(m[1]), int(m[2], 16), int(m[3], 16)) for m in map(LINE.match, open(log)) if m]
    fifo = [v for _, r, v in writes if r == 0x5F]
    if iv is None:
        if len(fifo) < 4:
            sys.exit("no burst sent in the log; give the IV with --iv")
        data = b"".join(struct.pack("<H", w) for w in fifo[:4])
        if data != burst(key, struct.unpack_from("<I", data, 1)[0]):
            sys.exit(f"the burst {data.hex()} isn't one for this key")
        iv = struct.unpack_from("<I", data, 1)[0]
    w71 = [(t, v) for t, r, v in writes if r == 0x71]
    if len(w71) < 20:
        sys.exit(f"only {len(w71)} REG_71 writes")
    best = None
    for off in range(w71[0][0] - 60, w71[0][0] + 5):
        p = Path(key, iv)
        err = max(abs(v - word(p.hz(t - off))) for t, v in w71 if t >= off)
        if best is None or err < best[1]:
            best = (off, err)
    off, err = best
    hz = [v * 10000 // 103244 for _, v in w71]
    print(f"IV {iv:08X}: {len(w71)} REG_71 writes over {w71[-1][0] - w71[0][0]} ms, "
          f"{min(hz)}-{max(hz)} Hz")
    print(f"path start fits best at {off} ms (first write {w71[0][0] - off} ms later); "
          f"worst error {err} (REG_71 units, {err * 10000 / 103244:.1f} Hz)")
    return off, err


def main():
    a = sys.argv[1:]
    if a[0] == "cfg":
        img = bytearray(open(a[1], "rb").read())
        key = a[3].encode()
        first, last = (int(a[4]), int(a[5])) if len(a) > 5 else (1, 40)
        payload = key.ljust(15, b"\0")
        rec = struct.pack("<HBB", 0xA36C, len(payload), 0) + payload.ljust(26, b"\0")
        rec += struct.pack("<H", crc_xmodem(rec))
        img[0x1E3000:0x1E4000] = b"\xff" * 0x1000
        img[0x1E3000:0x1E3020] = rec
        for c in range(first, last + 1):
            o = (c - 1) * 32 + 14
            img[o] = (img[o] & 3) | 60 << 2
        open(a[2], "wb").write(img)
    elif a[0] == "burst":
        print((burst(a[1], int(a[2], 0)) + b"\0\0").hex())
    elif a[0] == "path":
        p = Path(a[1], int(a[2], 0))
        for t in range(0, int(a[3]) if len(a) > 3 else 500, 10):
            print(t, p.hz(t))
    elif a[0] == "check":
        iv = int(a[a.index("--iv") + 1], 0) if "--iv" in a else None
        check(a[1], a[2], iv)
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
