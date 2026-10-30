#!/usr/bin/env python3
"""Back up the radio's whole external flash (2 MB) over the custom firmware's serial shell.

usage: flash_backup.py [--port /dev/ttyUSB0] [--size 0x200000] [--out backups/flash-DATE.bin]

Reads 4 KB at a time with the shell's 'X ADDR LEN' command (raw bytes + CRC-16/XMODEM), retries a
chunk whose CRC doesn't match, then re-reads 16 random chunks and compares them with the file.
Takes about 6.5 minutes at 57600 baud. Only reads: nothing on the radio changes.
"""
import argparse
import hashlib
import os
import random
import sys
import time

import serial

CHUNK = 4096


def crc16(data):
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = (crc << 1 ^ 0x1021 if crc & 0x8000 else crc << 1) & 0xFFFF
    return crc


def read_chunk(s, addr, n=CHUNK, tries=4):
    for attempt in range(tries):
        s.reset_input_buffer()
        s.write(f"X {addr:X} {n}\r".encode())
        data = s.read(n + 2)
        if len(data) == n + 2 and crc16(data[:n]) == (data[n] << 8 | data[n + 1]):
            return data[:n]
        time.sleep(0.3 * (attempt + 1))
        s.read(65536)                       # drain whatever is left of a broken reply
    raise SystemExit(f"chunk 0x{addr:06X}: no valid reply after {tries} tries")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/ttyUSB0")
    ap.add_argument("--size", type=lambda v: int(v, 0), default=0x200000)
    ap.add_argument("--out", default=time.strftime("backups/flash-%Y%m%d-%H%M.bin"))
    a = ap.parse_args()

    os.makedirs(os.path.dirname(a.out) or ".", exist_ok=True)
    s = serial.Serial(a.port, 57600, timeout=3)
    s.write(b"\r")
    time.sleep(0.3)
    s.read(65536)
    image = bytearray()
    t0 = time.time()
    for addr in range(0, a.size, CHUNK):
        image += read_chunk(s, addr)
        if addr % 0x40000 == 0x40000 - CHUNK:
            done = (addr + CHUNK) / a.size
            print(f"{done:4.0%}  {(time.time() - t0) / done * (1 - done) / 60:4.1f} min left", flush=True)
    open(a.out, "wb").write(image)

    for addr in random.sample(range(0, a.size, CHUNK), 16):
        if read_chunk(s, addr) != image[addr:addr + CHUNK]:
            raise SystemExit(f"verify failed at 0x{addr:06X}: the flash changed or reads are unstable")
    print(f"{a.out}: {len(image)} bytes, verified, sha256 {hashlib.sha256(image).hexdigest()}")


if __name__ == "__main__":
    sys.exit(main())
