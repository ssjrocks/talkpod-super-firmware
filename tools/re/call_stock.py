#!/usr/bin/env python3
"""Call one function of the stock firmware in Unicorn and log what it does to the hardware.

usage: call_stock.py STOCK.plain.bin FUNC [r0 [r1 [r2 [r3]]]] [--mem ADDR=HEXBYTES ...] [--bkreg REG=VAL ...]

STOCK.plain.bin is a decrypted stock image (loads at 0x08000400). Instead of running the whole
firmware (it runs on an RTOS, which Unicorn can't schedule), one function is called with the
given arguments on a zeroed RAM. Logged:
  BK4819 register writes/reads (by intercepting the stock bk4819_write_reg / read_reg),
  BK4819 GPIO changes (register 0x33), MCU GPIO pin set/clear/mode changes, other peripheral writes.
--mem pre-loads RAM (globals the function reads), --bkreg sets what BK4819 register reads return.
Delays are skipped. Addresses are for A36Plus-EN-V1.22-Super; see docs/stock-firmware.md.
"""
import argparse
import struct
import sys

from unicorn import (UC_ARCH_ARM, UC_HOOK_CODE, UC_HOOK_MEM_UNMAPPED, UC_HOOK_MEM_WRITE, UC_MODE_MCLASS,
                     UC_MODE_THUMB, Uc, UcError)
from unicorn.arm_const import (UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
                               UC_ARM_REG_R3, UC_ARM_REG_SP, UC_CPU_ARM_CORTEX_M4)

BK_WRITE, BK_READ = 0x08009C08, 0x08009800
DELAYS = (0x08002A36, 0x08002A52)            # delay_ms, delay_us
SENTINEL = 0x0800FFF0
PORTS = {0x48000000: "PA", 0x48000400: "PB", 0x48000800: "PC", 0x48000C00: "PD", 0x48001400: "PF"}
MODES = {0: "input", 1: "output", 2: "alt", 3: "analog"}


def pins(mask):
    return ",".join(str(b) for b in range(16) if (mask >> b) & 1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("func")
    ap.add_argument("args", nargs="*")
    ap.add_argument("--mem", action="append", default=[])
    ap.add_argument("--bkreg", action="append", default=[])
    ap.add_argument("--max", type=int, default=2_000_000)
    a = ap.parse_args()

    fw = open(a.image, "rb").read()
    uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M4)
    for base, size in ((0x08000000, 0x10000), (0x20000000, 0x4000), (0x40000000, 0x30000),
                       (0x48000000, 0x2000), (0xE0000000, 0x100000), (0x1FFFF000, 0x1000)):
        uc.mem_map(base, size)
    uc.mem_write(0x08000400, fw)
    uc.mem_write(SENTINEL, b"\xfe\xe7")      # b .
    for m in a.mem:
        addr, data = m.split("=")
        uc.mem_write(int(addr, 0), bytes.fromhex(data))
    bkregs = {int(k, 0): int(v, 0) for k, v in (r.split("=") for r in a.bkreg)}

    def ret(uc, value=None):
        if value is not None:
            uc.reg_write(UC_ARM_REG_R0, value)
        uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))

    def on_code(uc, addr, size, _):
        if addr == BK_WRITE:
            reg, val = uc.reg_read(UC_ARM_REG_R0) & 0xFF, uc.reg_read(UC_ARM_REG_R1) & 0xFFFF
            note = ""
            if reg == 0x33:
                note = f"   (BK GPIO out-disable {val >> 8:08b} out {val & 0xFF:08b})"
            print(f"BK  W 0x{reg:02X} = 0x{val:04X}{note}")
            bkregs[reg] = val
            ret(uc)
        elif addr == BK_READ:
            reg = uc.reg_read(UC_ARM_REG_R0) & 0xFF
            val = bkregs.get(reg, 0)
            print(f"BK  R 0x{reg:02X} -> 0x{val:04X}")
            ret(uc, val)
        elif addr in DELAYS:
            ret(uc)

    def on_write(uc, access, addr, size, value, _):
        port = addr & ~0x3FF
        off = addr & 0x3FF
        if port in PORTS:
            p = PORTS[port]
            if off == 0x18:
                if value & 0xFFFF:
                    print(f"GPIO {p} set   {pins(value & 0xFFFF)}")
                if value >> 16:
                    print(f"GPIO {p} clear {pins(value >> 16)}")
            elif off == 0x28:
                print(f"GPIO {p} clear {pins(value & 0xFFFF)}")
            elif off == 0x14:
                print(f"GPIO {p} out = 0x{value & 0xFFFF:04X}")
            elif off == 0x00:
                print(f"GPIO {p} mode reg = 0x{value:08X} "
                      + " ".join(f"{b}:{MODES[(value >> 2 * b) & 3]}" for b in range(16)))
            else:
                print(f"GPIO {p} +0x{off:02X} = 0x{value:X}")
        elif 0x40000000 <= addr < 0x40030000:
            print(f"PERIPH W 0x{addr:08X} = 0x{value:X} ({size} bytes)")

    uc.hook_add(UC_HOOK_CODE, on_code)
    uc.hook_add(UC_HOOK_MEM_WRITE, on_write, begin=0x40000000, end=0x48001FFF)
    uc.hook_add(UC_HOOK_MEM_UNMAPPED, lambda uc, acc, addr, s, v, _: print(f"!! unmapped 0x{addr:08X}") or False)
    regs = (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)
    for r, v in zip(regs, a.args):
        uc.reg_write(r, int(v, 0) & 0xFFFFFFFF)
    uc.reg_write(UC_ARM_REG_SP, 0x20003000)
    uc.reg_write(UC_ARM_REG_LR, SENTINEL | 1)
    try:
        uc.emu_start(int(a.func, 0) | 1, SENTINEL, count=a.max)
    except UcError as e:
        print(f"!! CPU error {e} at 0x{uc.reg_read(UC_ARM_REG_PC):08X}")
    pc = uc.reg_read(UC_ARM_REG_PC)
    print(f"-- returned r0=0x{uc.reg_read(UC_ARM_REG_R0):X}" if pc == SENTINEL else f"-- stopped at 0x{pc:08X}")


if __name__ == "__main__":
    sys.exit(main())
