#!/usr/bin/env python3
"""Disassemble a decrypted stock image (loaded at 0x08000400), annotating literal-pool loads.

usage: dis.py STOCK.plain.bin ADDR[:COUNT] ...      COUNT instructions (default 40), linear
       dis.py STOCK.plain.bin --calls FUNC ...       every call to FUNC with constant r0-r3
"""
import re
import struct
import sys

from capstone import CS_ARCH_ARM, CS_MODE_MCLASS, CS_MODE_THUMB, Cs

BASE = 0x08000400


def literal(fw, ins):
    m = re.search(r"\[pc, #(0x[0-9a-f]+|\d+)\]", ins.op_str)
    if not m:
        return None
    t = ((ins.address + 4) & ~3) + int(m.group(1), 0) - BASE
    return struct.unpack_from("<I", fw, t)[0] if 0 <= t <= len(fw) - 4 else None


def sweep(fw, md):
    out, off = [], 0
    while off < len(fw) - 2:
        ins = next(md.disasm(fw[off:off + 4], BASE + off), None)
        if ins is None:
            off += 2
            continue
        out.append(ins)
        off += ins.size
    return out


def const_arg(fw, ins_list, n, reg):
    for back in range(1, 12):
        p = ins_list[n - back]
        if p.mnemonic in ("bl", "blx", "bx", "pop", "pop.w", "b", "b.w"):
            return "?"
        if p.op_str.startswith(reg + ", "):
            ops = p.op_str.split(", ")
            if p.mnemonic in ("movs", "mov.w", "movw", "mov") and len(ops) == 2 and ops[1].startswith("#"):
                return ops[1][1:]
            if p.mnemonic.startswith("ldr") and "[pc" in p.op_str:
                v = literal(fw, p)
                return hex(v) if v is not None else "?"
            return "?"
    return "?"


def main():
    fw = open(sys.argv[1], "rb").read()
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS)
    args = sys.argv[2:]
    if args and args[0] == "--calls":
        ins_list = sweep(fw, md)
        for func in args[1:]:
            target = f"#{int(func, 0):#x}"
            for n, ins in enumerate(ins_list):
                if ins.mnemonic in ("bl", "b.w") and ins.op_str == target:
                    regs = ", ".join(const_arg(fw, ins_list, n, r) for r in ("r0", "r1", "r2", "r3"))
                    print(f"{ins.address:#x}: {func}({regs})")
        return
    for a in args:
        addr, count = (a.split(":") + ["40"])[:2]
        addr, count = int(addr, 0), int(count)
        print(f"--- {addr:#010x}")
        off = addr - BASE
        for ins in list(md.disasm(fw[off:off + count * 4], addr))[:count]:
            v = literal(fw, ins)
            note = f"   ; ={v:#x}" if v is not None else ""
            print(f"{ins.address:#x}: {ins.mnemonic:8s} {ins.op_str}{note}")


if __name__ == "__main__":
    main()
