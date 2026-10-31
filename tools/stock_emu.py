#!/usr/bin/env python3
"""Program mode and the stock settings log (src/stock.c), driven through tools/emu.py as CHIRP drives a
radio, and checked by stock's own rules.

usage: stock_emu.py build/a36se-fw.elf FLASH_IN WORKDIR
"""
import os
import struct
import subprocess
import sys

MAGIC = b"PROGRAMJC37U"


def crc16(b):
    c = 0
    for x in b:
        c ^= x << 8
        for _ in range(8):
            c = (c << 1 ^ 0x1021 if c & 0x8000 else c << 1) & 0xFFFF
    return c


def newest(flash, base, size, map_bytes):
    """What stock's load does: (slot, address, the record), or None when it would use defaults."""
    m = flash[base:base + map_bytes]
    slot = next((i * 8 + b for i in range(map_bytes) for b in range(8) if m[i] >> b & 1), 0xFF)
    a = base + 0x10 + slot * size
    r = flash[a:a + size]
    if crc16(r[:size - 2]) != struct.unpack_from("<H", r, size - 2)[0]:
        return None
    return slot, a, r


def R(addr, n=0x40):
    return b"R" + struct.pack(">HB", addr, n)


def W(addr, data):
    return b"W" + struct.pack(">HB", addr, len(data)) + data


def run(elf, flash_in, flash_out, raw, steps, ms):
    emu = os.path.join(os.path.dirname(os.path.abspath(__file__)), "emu.py")
    cmd = [sys.executable, emu, elf, "--ms", str(ms), "--flash", flash_in, "--flash-out", flash_out,
           "--serial-out", raw]
    for t, data in steps:
        cmd += ["--serial-in", f"{t}:{data.hex()}"]
    out = subprocess.run(cmd, capture_output=True, text=True).stdout
    return out, open(raw, "rb").read(), open(flash_out, "rb").read()


def main():
    elf, flash_in, work = sys.argv[1:4]
    before = open(flash_in, "rb").read()
    raw, f1, f2 = (os.path.join(work, n) for n in ("serial.bin", "flash1.bin", "flash2.bin"))

    # 1. what a Read sees: the live blocks, and plain flash beside them
    out, got, after_boot = run(elf, flash_in, f1, raw,
                               [(1500, MAGIC), (1600, b"F"), (1700, R(0x9000)), (1850, R(0x8000)),
                                (2000, R(0x9040)), (2150, R(0x0000)), (2300, b"E")], 3000)
    i = got.index(b"\x06", got.index(b"A36") - 2 if b"A36" in got else 0)
    i = got.index(b"A36CFW")
    print("ident", got[i:i + 8])
    p = i + 8
    blocks = {}
    for addr in (0x9000, 0x8000, 0x9040, 0x0000):
        assert got[p:p + 4] == R(addr), (hex(addr), got[p:p + 8].hex())
        blocks[addr] = got[p + 4:p + 4 + 0x40]
        p += 0x44
    s = newest(after_boot, 0x9000, 0x60, 6)
    v = newest(after_boot, 0x8000, 0x42, 8)
    print("settings log after boot: slot", s[0], "valid, at", hex(s[1]))
    assert blocks[0x9000] == s[2][:0x40], "R 0x9000 is not the newest settings record"
    assert blocks[0x8000] == v[2][:0x40], "R 0x8000 is not the newest VFO record"
    assert blocks[0x9040] == after_boot[0x9040:0x9080], "R 0x9040 is not plain flash"
    assert blocks[0x0000] == after_boot[:0x40], "R 0 is not plain flash"
    print("R 0x9000 / 0x8000 show the live records; R 0x9040 / 0 are plain flash: OK")
    print("  settings:", blocks[0x9000][:0x31].hex(" "))

    # 2. a Write: change one setting as CHIRP would, and write the VFO block unchanged
    edited = bytearray(blocks[0x9000])
    edited[0x17] = 1                        # Roger: BEEP
    out2, got2, after = run(elf, f1, f2, raw,
                            [(1500, MAGIC), (1600, b"F"), (1700, W(0x9000, bytes(edited))), (1900, W(0x8000, blocks[0x8000])),
                             (2100, R(0x9000)), (2300, b"E")], 3200)
    i = got2.index(b"A36CFW") + 8
    assert got2[i:i + 2] == b"\x06\x06", got2[i:i + 8].hex()
    assert got2[i + 2:i + 6] == R(0x9000)
    assert got2[i + 6:i + 6 + 0x40] == bytes(edited), "read back after a write differs"
    s2 = newest(after, 0x9000, 0x60, 6)
    v2 = newest(after, 0x8000, 0x42, 8)
    print("after W: settings slot", s[0], "->", s2[0], "| VFO slot", v[0], "->", v2[0], "(unchanged block adds no record)")
    assert s2[0] == s[0] + 1 and s2[2][:0x40] == bytes(edited) and v2[0] == v[0]
    assert s2[2][0x40:0x5E] == bytes(0x1E)
    diff = [hex(a) for a in range(0xB440) if after[a] != after_boot[a]]
    print("bytes changed in the window:", len(diff), "from", diff[0], "to", diff[-1])
    print("old slots untouched:", after[0x9010:s[1] + 0x60] == after_boot[0x9010:s[1] + 0x60])
    print("OK")


if __name__ == "__main__":
    main()
