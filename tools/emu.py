#!/usr/bin/env python3
"""Run the firmware in Unicorn: simulated keys, serial output, and a PNG of what it draws on the LCD.

usage: emu.py build/a36se-fw.elf [--ms 3000] [--keys 200:UP:100,400:5:900,1800:PTT:300] [--png lcd.png]

--keys is a list of start_ms:KEY:duration_ms (KEY as in drivers/keypad.c key_name()). Time is
simulated: SysTick is faked by bumping `ticks` every INSN_PER_MS instructions (about 48 MHz at two
cycles per instruction; instructions are counted per block, as block bytes / 2), so timings are
only roughly real. Needs unicorn (tools/requirements-emu.txt).
"""
import argparse
import struct
import subprocess
import sys
import zlib

from unicorn import (UC_ARCH_ARM, UC_HOOK_BLOCK, UC_HOOK_MEM_READ, UC_HOOK_MEM_UNMAPPED, UC_HOOK_MEM_WRITE,
                     UC_MODE_MCLASS, UC_MODE_THUMB, Uc, UcError)
from unicorn.arm_const import UC_ARM_REG_PC, UC_ARM_REG_SP, UC_CPU_ARM_CORTEX_M4

INSN_PER_MS = 24000
X_OFFSET = 32                      # drivers/lcd.c
GPIOA, GPIOB = 0x48000000, 0x48000400
SPI1 = 0x40003800
USART0 = 0x40013800                # older layout on this MCU: STAT +0, DATA +4

MATRIX = ["MENU", "UP", "DOWN", "BACK", "1", "2", "3", "A/B", "4", "5", "6", "0", "7", "8", "9", "FM"]
ROW_PINS = [3, 4, 5, 6]
COL_PINS = [11, 9, 14, 8]
SIDE = {"TOP": 0, "SIDE1": 1, "SIDE2": 2}    # side keys pull these columns low directly


def symbols(elf):
    out = {}
    for line in subprocess.run(["arm-none-eabi-nm", elf], capture_output=True, text=True).stdout.splitlines():
        parts = line.split()
        if len(parts) == 3:
            out[parts[2]] = int(parts[0], 16)
    return out


def write_png(path, pixels, w, h, scale):
    rows = []
    for y in range(h * scale):
        line = bytearray([0])
        for x in range(w):
            v = pixels[(y // scale) * w + x]
            rgb = bytes((((v >> 11) & 0x1F) * 255 // 31, ((v >> 5) & 0x3F) * 255 // 63, (v & 0x1F) * 255 // 31))
            line += rgb * scale
        rows.append(bytes(line))

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data))
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w * scale, h * scale, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b"")
    open(path, "wb").write(png)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("elf")
    ap.add_argument("--ms", type=int, default=3000)
    ap.add_argument("--keys", default="")
    ap.add_argument("--png")
    args = ap.parse_args()

    binf = args.elf.rsplit(".", 1)[0] + ".bin"
    image = open(binf, "rb").read()
    ticks_addr = symbols(args.elf)["ticks"]
    script = []
    for item in filter(None, args.keys.split(",")):
        start, key, dur = item.split(":")
        script.append((int(start), int(start) + int(dur), key))

    uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M4)
    for base, size in ((0x08000000, 0x10000), (0x20000000, 0x4000), (0x40000000, 0x30000),
                       (0x48000000, 0x2000), (0xE0000000, 0x100000), (0x1FFFF000, 0x1000)):
        uc.mem_map(base, size)
    uc.mem_write(0x08000400, image)

    st = {"n": 0, "ms": 0, "octl": {GPIOA: 0, GPIOB: 0}, "cmd": None, "params": [], "win": [0, 0, 0, 0],
          "cx": 0, "cy": 0, "hi": None}
    gram = [0] * (162 * 162)
    serial = bytearray()
    log = []

    def pressed():
        return {k for a, b, k in script if a <= st["ms"] < b}

    def on_block(uc, addr, size, _):
        st["n"] += max(1, size // 2)
        while st["n"] >= (st["ms"] + 1) * INSN_PER_MS:
            st["ms"] += 1
            t = struct.unpack("<I", uc.mem_read(ticks_addr, 4))[0]
            uc.mem_write(ticks_addr, struct.pack("<I", (t + 1) & 0xFFFFFFFF))
        if st["ms"] >= args.ms:
            uc.emu_stop()

    def on_read(uc, access, addr, size, value, _):
        if addr == SPI1 + 0x08:
            uc.mem_write(addr, struct.pack("<I", 0x02))            # TBE, not busy
        elif addr == USART0:
            uc.mem_write(addr, struct.pack("<I", 0xC0))            # TXE | TC, nothing received
        elif addr == GPIOB + 0x10:                                 # ISTAT: key matrix
            keys = pressed()
            rows_low = [r for r, pin in enumerate(ROW_PINS) if not (st["octl"][GPIOB] >> pin) & 1]
            v = 0xFFFF
            for k in keys:
                if k in SIDE:
                    v &= ~(1 << COL_PINS[SIDE[k]])
                elif k in MATRIX:
                    r, c = divmod(MATRIX.index(k), 4)
                    if r in rows_low:
                        v &= ~(1 << COL_PINS[c])
            uc.mem_write(addr, struct.pack("<I", v))
        elif addr == GPIOA + 0x10:                                 # ISTAT: PTT on PA10
            uc.mem_write(addr, struct.pack("<I", 0xFFFF & ~(1 << 10 if "PTT" in pressed() else 0)))

    def lcd_byte(b):
        o = st["octl"][GPIOB]
        if (o >> 2) & 1:                                           # CS high
            return
        if not (o >> 10) & 1:                                      # DC low: command
            st["cmd"], st["params"] = b, []
            if b == 0x2C:
                st["cx"], st["cy"], st["hi"] = st["win"][0], st["win"][2], None
            return
        if st["cmd"] in (0x2A, 0x2B):
            st["params"].append(b)
            if len(st["params"]) == 4:
                p = st["params"]
                lo, hi = p[0] << 8 | p[1], p[2] << 8 | p[3]
                if st["cmd"] == 0x2A:
                    st["win"][0:2] = [lo, hi]
                else:
                    st["win"][2:4] = [lo, hi]
        elif st["cmd"] == 0x2C:
            if st["hi"] is None:
                st["hi"] = b
                return
            v = st["hi"] << 8 | b
            st["hi"] = None
            x, y = st["cx"], st["cy"]
            if x < 162 and y < 162:
                gram[y * 162 + x] = v
            x += 1
            if x > st["win"][1]:
                x, y = st["win"][0], y + 1
            st["cx"], st["cy"] = x, y

    def on_write(uc, access, addr, size, value, _):
        if addr == USART0 + 0x04:
            serial.append(value & 0xFF)
            if value & 0xFF == 0x0A:
                log.append((st["ms"], serial.decode("latin-1").rstrip()))
                serial.clear()
        elif addr in (GPIOA + 0x18, GPIOB + 0x18):                 # BOP
            port = addr - 0x18
            st["octl"][port] = (st["octl"][port] | (value & 0xFFFF)) & ~(value >> 16)
        elif addr in (GPIOA + 0x28, GPIOB + 0x28):                 # BC
            st["octl"][addr - 0x28] &= ~(value & 0xFFFF)
        elif addr in (GPIOA + 0x14, GPIOB + 0x14):                 # OCTL
            st["octl"][addr - 0x14] = value & 0xFFFF
        elif addr == SPI1 + 0x0C:
            lcd_byte(value & 0xFF)

    uc.hook_add(UC_HOOK_BLOCK, on_block)
    uc.hook_add(UC_HOOK_MEM_READ, on_read, begin=0x40000000, end=0x48001FFF)
    uc.hook_add(UC_HOOK_MEM_WRITE, on_write, begin=0x40000000, end=0x48001FFF)
    uc.hook_add(UC_HOOK_MEM_UNMAPPED, lambda uc, a, addr, s, v, _: print(f"!! unmapped {addr:#x}") or False)
    sp, reset = struct.unpack_from("<II", image, 0)
    uc.reg_write(UC_ARM_REG_SP, sp)
    try:
        uc.emu_start(reset | 1, 0xFFFFFFFF)
    except UcError as e:
        print(f"!! CPU error {e} at {uc.reg_read(UC_ARM_REG_PC):#x}")
    if serial:
        log.append((st["ms"], serial.decode("latin-1")))
    for ms, line in log:
        print(f"{ms:6d} ms  {line}")
    print(f"ran ~{st['n']} instructions, {st['ms']} simulated ms")
    if args.png:
        visible = [gram[y * 162 + x + X_OFFSET] for y in range(128) for x in range(128)]
        write_png(args.png, visible, 128, 128, 3)
        print(f"LCD image: {args.png}")


if __name__ == "__main__":
    sys.exit(main())
