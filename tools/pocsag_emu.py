#!/usr/bin/env python3
"""POCSAG test helpers for tools/emu.py.

  pocsag_emu.py rx RIC TEXT [numeric]
      print the hex for emu.py --fsk-rx: the codewords after the first sync codeword (which the
      BK4819 matches itself), MSB first, in one batch or more
  pocsag_emu.py tx LOG
      decode what the firmware sent from an emu.py --bk log (the REG_5F FIFO writes of a send)
"""
import sys

SYNC, IDLE = 0x7CD215D8, 0x7A89C197
NUMERIC = "0123456789*U -)("


def encode(info):
    v = info << 10
    for i in range(30, 9, -1):
        if v >> i & 1:
            v ^= 0x769 << (i - 10)
    cw = (info << 10 | v) << 1
    return cw | bin(cw).count("1") & 1


def codewords(ric, text, numeric):
    cws = [IDLE] * ((ric & 7) * 2) + [encode((ric >> 3) << 2 | (0 if numeric else 3))]
    bits = []
    for c in text:
        v = NUMERIC.index(c) if numeric else ord(c)
        bits += [v >> b & 1 for b in range(4 if numeric else 7)]
    while len(bits) % 20:                       # pad: spaces (numeric), NUL (alphanumeric)
        bits += [0, 0, 1, 1][:20 - len(bits) % 20] if numeric else [0]
    for i in range(0, len(bits), 20):
        cws.append(encode(1 << 20 | int("".join(map(str, bits[i:i + 20])), 2)))
    cws.append(IDLE)
    cws += [IDLE] * (-len(cws) % 16)
    out = []
    for b in range(0, len(cws), 16):
        if b:
            out.append(SYNC)
        out += cws[b:b + 16]
    return out


def decode(cws, first_batch_synced=True):
    pages, cur, pos = [], None, 0
    for cw in cws:
        if cw == SYNC:
            pos = 0
            continue
        if encode(cw >> 11) != cw:
            print(f"  bad codeword {cw:08X}")
        if cw == IDLE:
            cur = None
        elif not cw >> 31:
            func = cw >> 11 & 3
            cur = [((cw >> 13 & 0x3FFFF) << 3) | pos // 2, func, []]
            pages.append(cur)
        elif cur:
            cur[2] += [cw >> (11 + b) & 1 for b in range(19, -1, -1)]
        pos += 1
    for ric, func, bits in pages:
        n = 4 if func == 0 else 7
        chars = [sum(bits[i + b] << b for b in range(n)) for i in range(0, len(bits) - n + 1, n)]
        text = "".join(NUMERIC[c] if func == 0 else chr(c) for c in chars if func == 0 or 32 <= c < 127)
        print(f"RIC {ric} function {func}: {text!r}")


def main():
    if sys.argv[1] == "rx":
        cws = codewords(int(sys.argv[2]), sys.argv[3], len(sys.argv) > 4)
        print(b"".join(cw.to_bytes(4, "big") for cw in cws).hex())
    else:
        data = b""
        for line in open(sys.argv[2]):
            p = line.split()
            if len(p) >= 7 and p[2] == "BK" and p[3] == "W" and p[4] == "0x5F":
                w = int(p[6], 16)
                data += bytes([w & 255, w >> 8])
        pre = len(data) - len(data.lstrip(b"\xaa"))
        print(f"{len(data)} bytes, {pre} preamble bytes (+ 16 + 4 from the chip)")
        body = data[pre:]
        cws = [int.from_bytes(body[i:i + 4], "big") for i in range(0, len(body) - 3, 4)]
        print(f"{len(cws)} codewords, syncs at {[i for i, c in enumerate(cws) if c == SYNC]}")
        decode(cws)


if __name__ == "__main__":
    main()
