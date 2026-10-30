#!/usr/bin/env python3
"""Build the firmware's sound pack from the stock voice prompts and write it to the radio.

usage: soundpack.py BACKUP.bin [--port /dev/ttyUSB0] [--dry-run]

Keeps the prompts listed in KEEP (stock numbers, see extract_voice.py), packs them as
"SNDP", u16 count, u16 sample rate, count x {u16 id, u16 0, u32 offset, u32 length}, then the
ADPCM data, and writes the pack at 0x110000 over the custom firmware's serial shell. First it
erases the whole old voice area 0x110000-0x191FFF: the other prompts are discarded (they stay in
the backup). Needs firmware v0.93 or later ('E', 'W', 'X' and 'P' shell commands).
"""
import argparse
import struct
import sys
import time

import serial

from extract_voice import DATA_BASE, FIRST, INDEX, RATE
from flash_backup import crc16, read_chunk

KEEP = [120, 121, 122, 137, 138, 139, 149, 151, 153, 156]
PACK_ADDR = 0x110000
VOICE_END = 0x192000            # the old prompts ended at 0x1911AA


def build(flash):
    data, entries = b"", []
    header = 8 + 12 * len(KEEP)
    for n in KEEP:
        length, off = struct.unpack_from("<II", flash, INDEX + (n - FIRST) * 8)
        entries.append(struct.pack("<HHII", n, 0, header + len(data), length))
        data += flash[off + DATA_BASE:off + DATA_BASE + length]
    return b"SNDP" + struct.pack("<HH", len(KEEP), RATE) + b"".join(entries) + data


def command(s, line, wait=3.0):
    s.reset_input_buffer()
    s.write((line + "\r").encode())
    t0, reply = time.time(), b""
    while time.time() - t0 < wait and not reply.endswith(b"\n"):
        reply += s.read(64)
    return reply.decode("latin-1").strip()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("backup")
    ap.add_argument("--port", default="/dev/ttyUSB0")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()

    pack = build(open(a.backup, "rb").read())
    print(f"pack: {len(KEEP)} sounds, {len(pack)} bytes")
    if a.dry_run:
        return
    s = serial.Serial(a.port, 57600, timeout=0.2)
    s.write(b"\r")
    time.sleep(0.3)
    s.read(65536)

    for addr in range(PACK_ADDR, VOICE_END, 0x1000):
        reply = command(s, f"E {addr:X}")
        if reply != "ok":
            raise SystemExit(f"erase 0x{addr:06X}: {reply!r}")
    print(f"erased 0x{PACK_ADDR:06X}-0x{VOICE_END - 1:06X}")

    for off in range(0, len(pack), 256):
        chunk = pack[off:off + 256]
        for attempt in range(3):
            s.reset_input_buffer()
            s.write(f"W {PACK_ADDR + off:X} {len(chunk)}\r".encode())
            time.sleep(0.05)
            s.write(chunk + struct.pack(">H", crc16(chunk)))
            t0, reply = time.time(), b""
            while time.time() - t0 < 3 and not reply.endswith(b"\n"):
                reply += s.read(64)
            if reply.strip() == b"ok":
                break
        else:
            raise SystemExit(f"write 0x{PACK_ADDR + off:06X}: {reply!r}")
    print("written")

    s.timeout = 3                           # a 4 KB read takes 0.72 s at 57600 baud
    back = b""
    for off in range(0, len(pack), 4096):
        back += read_chunk(s, PACK_ADDR + off)
    if back[:len(pack)] != pack:
        raise SystemExit("verify failed: the pack in flash differs")
    print("verified")
    print("play 153:", command(s, "P 153"))


if __name__ == "__main__":
    sys.exit(main())
