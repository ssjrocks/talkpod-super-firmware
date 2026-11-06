#!/usr/bin/env python3
"""Set stock settings (src/stock.h names) in a flash image's settings log, as the firmware saves them.

usage: stock_set.py IN.bin OUT.bin NAME=VALUE ...      e.g. stock_set.py tests/data/flash-test.bin build/f.bin ste=1

A new record goes into the next slot of the log at 0x9000 (tools/stock_emu.py's newest()); the
offsets come from the stock_t struct in src/stock.h. Put OUT under build/.
"""
import re
import struct
import sys

from stock_emu import crc16, newest

BASE, SIZE, MAP = 0x9000, 0x60, 6


def offsets():
    out, off = {}, 0
    body = open("src/stock.h").read().split("typedef struct", 1)[1].split("} stock_t;", 1)[0]
    for names in re.findall(r"uint8_t ([^;\[]+);", body):
        for name in names.split(","):
            out[name.strip()] = off
            off += 1
    return out


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    flash = bytearray(open(sys.argv[1], "rb").read())
    found = newest(bytes(flash), BASE, SIZE, MAP)
    if not found:
        sys.exit("no valid settings record in " + sys.argv[1])
    slot, addr, record = found
    rec, offs = bytearray(record), offsets()
    for item in sys.argv[3:]:
        name, value = item.split("=")
        rec[offs[name]] = int(value, 0)
    rec[SIZE - 2:] = struct.pack("<H", crc16(bytes(rec[:SIZE - 2])))
    nxt = slot + 1
    if nxt >= MAP * 8 or flash[BASE + 0x10 + nxt * SIZE] != 0xFF:
        sys.exit("the log is full; use an image with room")
    flash[BASE + 0x10 + nxt * SIZE:BASE + 0x10 + (nxt + 1) * SIZE] = rec
    flash[BASE + slot // 8] &= ~(1 << (slot & 7)) & 0xFF
    open(sys.argv[2], "wb").write(flash)
    print(f"{sys.argv[2]}: record {nxt}, " + ", ".join(sys.argv[3:]))


if __name__ == "__main__":
    main()
