#!/usr/bin/env python3
"""Make a flash image for emu.py with the sound pack in it, so `emu.py --voice` has sounds to play.

usage: voice_flash.py BACKUP.bin OUT.bin [BASE.bin]

BACKUP is a 2 MB flash backup with the stock voice prompts (backups/, which is not in the repository).
The pack is built from it as soundpack.py builds it and written at 0x110000 into BASE (default
tests/data/flash-test.bin). Put OUT under build/.
"""
import sys

from soundpack import PACK_ADDR, build


def main():
    if len(sys.argv) not in (3, 4):
        sys.exit(__doc__)
    pack = build(open(sys.argv[1], "rb").read())
    flash = bytearray(open(sys.argv[3] if len(sys.argv) == 4 else "tests/data/flash-test.bin", "rb").read())
    flash[PACK_ADDR:PACK_ADDR + len(pack)] = pack
    open(sys.argv[2], "wb").write(flash)
    print(f"{sys.argv[2]}: sound pack of {len(pack)} bytes at {PACK_ADDR:#x}")


if __name__ == "__main__":
    main()
