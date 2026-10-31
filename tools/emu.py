#!/usr/bin/env python3
"""Run the firmware in Unicorn: simulated keys, serial output, and a PNG of what it draws on the LCD.

usage: emu.py build/a36se-fw.elf [--ms 3000] [--keys 200:UP:100,400:5:900,1800:PTT:300] [--png lcd.png]
              [--bk] [--bkreg 0x67=0x50] [--bkreg-at MS:0x0C=0x0002 ...] [--switch-off-at MS]
              [--flash tests/data/flash-test.bin] [--signal-at 47642500 ...]

--keys is a list of start_ms:KEY:duration_ms (KEY as in drivers/keypad.c key_name()). The BK4819
is modelled on its bit-banged pins (writes are stored, reads answered from them or --bkreg); --bk
logs every register access; --bkreg-at changes a register value at a time (a signal opening the
squelch, a frequency-counter result). The power switch (PF7) reads on until --switch-off-at.
--flash serves an image of the external SPI flash (W25Q16 READ / JEDEC ID); erased otherwise.
--signal-at puts a steady signal on a frequency (10 Hz units): while the BK4819 is tuned there its
squelch bit (REG_0C bit 1) is set and its noise (REG_65) reads 10. --fsk-rx MS:HEX delivers an
FSK packet (sync, five FIFO words, packet complete; the rest is left in the FIFO), and an FSK send
(REG_59 bit 11) reports its FIFO almost empty every 50 ms and finishes when REG_5D bytes would
have taken at REG_72's bit rate. --pins PC15,PA13 logs those outputs among the
BK4819 accesses (with --bk); --hwver sets the bootloader's board tag (default 02). --serial-in
MS:HEX delivers bytes to the firmware's serial receive buffer from that time on (about 57600 baud),
and --serial-out FILE saves all it sends, raw. Time is
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
GPIOA, GPIOB, GPIOC, GPIOF = 0x48000000, 0x48000400, 0x48000800, 0x48001400
SPI1 = 0x40003800
SPI0 = 0x40013000
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


def sized_symbol(elf, name, size):
    """The address of the symbol of that name and size (static names can repeat)."""
    out = subprocess.run(["arm-none-eabi-nm", "-S", elf], capture_output=True, text=True).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 4 and parts[3] == name and int(parts[1], 16) == size:
            return int(parts[0], 16)
    return None


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
    ap.add_argument("--bk", action="store_true")
    ap.add_argument("--bkreg", action="append", default=[])
    ap.add_argument("--switch-off-at", type=int, default=10**9)
    ap.add_argument("--bkreg-at", action="append", default=[])
    ap.add_argument("--flash")
    ap.add_argument("--flash-out", help="save the flash image after the run (writes included)")
    ap.add_argument("--signal-at", action="append", default=[], type=int)
    ap.add_argument("--adc", type=lambda v: int(v, 0), default=0x0A20, help="ADC reading (12-bit)")
    ap.add_argument("--fsk-rx", action="append", default=[],
                    help="MS:HEX an FSK packet arrives (sync, a few FIFO words, then complete)")
    ap.add_argument("--serial-in", action="append", default=[], help="MS:HEX bytes to receive")
    ap.add_argument("--serial-out", help="save what the firmware sends, raw")
    ap.add_argument("--hwver", default="02", help="board tag at 0x08000300 ('' for none)")
    ap.add_argument("--pins", default="", help="log changes of these outputs with --bk, e.g. PC15,PB0,PA13")
    args = ap.parse_args()

    binf = args.elf.rsplit(".", 1)[0] + ".bin"
    image = open(binf, "rb").read()
    syms = symbols(args.elf)
    ticks_addr = syms["ticks"]
    rx_at = sized_symbol(args.elf, "rx_buf", 256)
    rx_due = sorted((int(i.split(":")[0]), bytes.fromhex(i.split(":")[1])) for i in args.serial_in)
    rx_now = bytearray()
    serial_raw = bytearray()
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
    if args.hwver:
        uc.mem_write(0x08000300, b"HWVER" + args.hwver.encode())    # the bootloader's board tag

    st = {"n": 0, "ms": 0, "octl": {GPIOA: 0, GPIOB: 0}, "cmd": None, "params": [], "win": [0, 0, 0, 0],
          "cx": 0, "cy": 0, "hi": None, "bk": None}
    st["octl"].update({GPIOC: 0, GPIOF: 0})
    bkregs = {int(k, 0): int(v, 0) for k, v in (r.split("=") for r in args.bkreg)}
    bk_sched = []
    for item in args.bkreg_at:
        when, rv = item.split(":")
        reg, val = rv.split("=")
        bk_sched.append((int(when), int(reg, 0), int(val, 0)))
    bk_sched.sort()
    bklog = []
    fsk = []                                # (ms, flags to hand out, words)
    fifo = []                               # received words not read yet
    fsk_tx = {"done_at": None, "empty_at": None}   # an FSK send (REG_59 bit 11): FIFO refills, end
    for item in args.fsk_rx:
        when, hx = item.split(":")
        data = bytes.fromhex(hx).ljust(46, b"\0")
        data += b"\0" * (len(data) & 1)
        words = [data[i] | data[i + 1] << 8 for i in range(0, len(data), 2)]
        fsk.append([int(when), [0x0002] + [0x1000] * 5 + [0x2000], words])
    fsk.sort()
    ports = {"A": GPIOA, "B": GPIOB, "C": GPIOC, "F": GPIOF}
    watched = [(name, ports[name[1]], int(name[2:])) for name in args.pins.split(",") if name]
    flash = bytearray(open(args.flash, "rb").read()) if args.flash else bytearray()
    fl = {"cmd": [], "out": b"", "rx": 0, "log": []}
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
            while rx_due and rx_due[0][0] <= st["ms"]:
                rx_now.extend(rx_due.pop(0)[1])
            if rx_now:                                 # what the USART interrupt does, 5 bytes a ms
                head = uc.mem_read(syms["rx_head"], 1)[0]
                for _ in range(min(5, len(rx_now))):
                    uc.mem_write(rx_at + head, bytes([rx_now.pop(0)]))
                    head = (head + 1) & 0xFF
                uc.mem_write(syms["rx_head"], bytes([head]))
        if st["ms"] >= args.ms:
            uc.emu_stop()

    def on_read(uc, access, addr, size, value, _):
        while bk_sched and bk_sched[0][0] <= st["ms"]:
            _, reg, val = bk_sched.pop(0)
            bkregs[reg] = val
        if addr == SPI1 + 0x08:
            uc.mem_write(addr, struct.pack("<I", 0x02))            # TBE, not busy
        elif addr == SPI0 + 0x08:
            uc.mem_write(addr, struct.pack("<I", 0x03))            # TBE | RBNE
        elif addr == 0x40012400:
            uc.mem_write(addr, struct.pack("<I", 0x02))            # ADC conversion done
        elif addr == 0x4001244C:
            uc.mem_write(addr, struct.pack("<I", args.adc))
        elif addr == SPI0 + 0x0C:
            uc.mem_write(addr, struct.pack("<I", fl["rx"]))
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
        elif addr == GPIOA + 0x10:                                 # ISTAT: PTT on PA10, BK4819 SDA PA3
            v = 0xFFFF & ~(1 << 10 if "PTT" in pressed() else 0)
            bk = st["bk"]
            if bk and bk["n"] >= 8 and bk["read"] and not bit_of(bk["val"], 15 - bk["rd_n"]):
                v &= ~(1 << 3)
            uc.mem_write(addr, struct.pack("<I", v))
        elif addr == GPIOF + 0x10:                                 # ISTAT: power switch PF7, high = off
            uc.mem_write(addr, struct.pack("<I", 0xFFFF if st["ms"] >= args.switch_off_at else 0xFF7F))
        elif addr in (GPIOC + 0x10,):
            uc.mem_write(addr, struct.pack("<I", 0xFFFF))

    def bk_value(reg):
        if fsk_tx["done_at"] is not None and st["ms"] >= min(fsk_tx["done_at"], fsk_tx["empty_at"]):
            if reg == 0x0C:
                return bkregs.get(0x0C, 0) | 1
            if reg == 0x02:
                if st["ms"] >= fsk_tx["done_at"]:
                    fsk_tx["done_at"] = None
                    return 0x8000
                fsk_tx["empty_at"] += 50
                return 0x4000
        if fsk and fsk[0][0] <= st["ms"]:
            pkt = fsk[0]
            if reg == 0x0C:
                return bkregs.get(0x0C, 0) | 1
            if reg == 0x02:
                if pkt[2]:
                    fifo[:] = pkt[2]
                    pkt[2] = []
                flag = pkt[1].pop(0)
                if not pkt[1]:
                    fsk.pop(0)
                return flag
        if reg == 0x5F:
            return fifo.pop(0) if fifo else 0
        tuned = bkregs.get(0x39, 0) << 16 | bkregs.get(0x38, 0)
        if tuned in args.signal_at and reg in (0x0C, 0x65):
            return (bkregs.get(0x0C, 0) | 0x0002) if reg == 0x0C else 10
        return bkregs.get(reg, 0)

    def bit_of(v, n):
        return (v >> n) & 1

    def bk_pins(before_a, before_c):
        """BK4819 frame decoder: CS = PC13, SCK = PA2, SDA = PA3; act on SCK rising edges."""
        a, c = st["octl"][GPIOA], st["octl"][GPIOC]
        cs_now, cs_was = (c >> 13) & 1, (before_c >> 13) & 1
        if cs_was and not cs_now:
            st["bk"] = {"n": 0, "v": 0, "read": False, "reg": 0, "rd_n": -1}
        elif cs_now and not cs_was and st["bk"]:
            bk = st["bk"]
            if bk["n"] == 24 and not bk["read"]:
                val = bk["v"] & 0xFFFF
                bkregs[bk["reg"]] = val
                if bk["reg"] == 0x59 and val & 0x0800:      # as long as REG_5D bytes take at REG_72's rate
                    baud = max(1, bkregs.get(0x72, 12389) * 10000 // 103244)
                    n = 20 + (bkregs.get(0x5D, 0x2C00) >> 8)
                    fsk_tx["done_at"] = st["ms"] + n * 8 * 1000 // baud
                    fsk_tx["empty_at"] = st["ms"] + 50
                bklog.append((st["ms"], "W", bk["reg"], val, st["n"] * 2))
            elif bk["read"]:
                bklog.append((st["ms"], "R", bk["reg"], bk["val"]))
            st["bk"] = None
        bk = st["bk"]
        if bk is None or cs_now:
            return
        if (a >> 2) & 1 and not (before_a >> 2) & 1:               # SCK rising
            if bk["n"] < 8 or not bk["read"]:
                bk["v"] = (bk["v"] << 1) | ((a >> 3) & 1)
                bk["n"] += 1
                if bk["n"] == 8:
                    bk["read"] = bool(bk["v"] & 0x80)
                    bk["reg"] = bk["v"] & 0x7F
                    if bk["read"]:
                        bk["val"] = bk_value(bk["reg"])     # once per read: FIFO-like registers
                    bk["v"] = 0
            else:
                bk["rd_n"] += 1
                bk["n"] += 1

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

    def flash_byte(b):
        """W25Q16 model: CS = PA4. READ 03, JEDEC ID 9F, status 05 (never busy), write enable 06,
        page program 02 (bits only go 1 -> 0, wraps in the page), sector erase 20 (on CS high)."""
        if (st["octl"][GPIOA] >> 4) & 1:
            return 0xFF
        c = fl["cmd"]
        c.append(b)
        if c[0] == 0x9F:
            return [0xFF, 0x5E, 0x60, 0x15][min(len(c) - 1, 3)]
        if c[0] == 0x05:
            return 0x00
        if c[0] == 0x06:
            fl["wel"] = True
        if c[0] == 0x03 and len(c) > 4:
            a = (c[1] << 16 | c[2] << 8 | c[3]) + len(c) - 5
            return flash[a] if a < len(flash) else 0xFF
        if c[0] == 0x02 and len(c) > 4 and fl.get("wel"):
            base = c[1] << 16 | c[2] << 8 | c[3]
            a = (base & ~0xFF) | ((base + len(c) - 5) & 0xFF)
            if a >= len(flash):
                flash.extend(b"\xff" * (a + 1 - len(flash)))
            flash[a] &= b
            fl["log"].append(("program", a))
        return 0xFF

    def flash_cs_high():
        c = fl["cmd"]
        if c and c[0] == 0x20 and len(c) >= 4 and fl.get("wel"):
            a = (c[1] << 16 | c[2] << 8 | c[3]) & ~0xFFF
            if a + 0x1000 > len(flash):
                flash.extend(b"\xff" * (a + 0x1000 - len(flash)))
            flash[a:a + 0x1000] = b"\xff" * 0x1000
            fl["log"].append(("erase", a))
        if c and c[0] in (0x02, 0x20):
            fl["wel"] = False
        fl["cmd"] = []

    def pin_changes(before):
        for name, port, pin in watched:
            was, now = (before[port] >> pin) & 1, (st["octl"][port] >> pin) & 1
            if was != now:
                bklog.append((st["ms"], "P", name, now))

    def on_write(uc, access, addr, size, value, _):
        if watched and addr & ~0x3FF in st["octl"]:
            before = dict(st["octl"])
            on_write_port(uc, access, addr, size, value, _)
            pin_changes(before)
        else:
            on_write_port(uc, access, addr, size, value, _)

    def on_write_port(uc, access, addr, size, value, _):
        if addr == SPI0 + 0x0C:
            fl["rx"] = flash_byte(value & 0xFF)
        elif addr == USART0 + 0x04:
            serial.append(value & 0xFF)
            serial_raw.append(value & 0xFF)
            if value & 0xFF == 0x0A:
                log.append((st["ms"], serial.decode("latin-1").rstrip()))
                serial.clear()
        elif (addr & 0xFFF) in (0x018, 0x418, 0x818, 0x1418) and (addr & ~0x3FF) in st["octl"]:   # BOP
            port = addr - 0x18
            ba, bc = st["octl"][GPIOA], st["octl"][GPIOC]
            st["octl"][port] = (st["octl"][port] | (value & 0xFFFF)) & ~(value >> 16)
            bk_pins(ba, bc)
            if (st["octl"][GPIOA] >> 4) & 1:
                flash_cs_high()
        elif (addr & ~0x3FF) in st["octl"] and (addr & 0x3FF) == 0x28:                         # BC
            ba, bc = st["octl"][GPIOA], st["octl"][GPIOC]
            st["octl"][addr - 0x28] &= ~(value & 0xFFFF)
            bk_pins(ba, bc)
            if (st["octl"][GPIOA] >> 4) & 1:
                flash_cs_high()
        elif (addr & ~0x3FF) in st["octl"] and (addr & 0x3FF) == 0x14:                         # OCTL
            ba, bc = st["octl"][GPIOA], st["octl"][GPIOC]
            st["octl"][addr - 0x14] = value & 0xFFFF
            bk_pins(ba, bc)
        elif addr == SPI1 + 0x0C:
            lcd_byte(value & 0xFF)

    uc.hook_add(UC_HOOK_BLOCK, on_block)
    uc.hook_add(UC_HOOK_MEM_READ, on_read, begin=0x40000000, end=0x48001FFF)
    uc.hook_add(UC_HOOK_MEM_READ,                                  # DWT cycle counter: 2 per instruction
                lambda uc, a, addr, sz, v, _: uc.mem_write(addr, struct.pack("<I", st["n"] * 2 & 0xFFFFFFFF)),
                begin=0xE0001004, end=0xE0001007)
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
    if args.serial_out:
        open(args.serial_out, "wb").write(serial_raw)
    if args.bk:
        for ms, kind, reg, val, *rest in bklog:
            if kind == "P":
                print(f"{ms:6d} ms  pin {reg} = {val}")
            else:
                cyc = f"  @{rest[0]}" if rest else ""
                print(f"{ms:6d} ms  BK {kind} 0x{reg:02X} {'=' if kind == 'W' else '->'} 0x{val:04X}{cyc}")
    for ms, line in log:
        print(f"{ms:6d} ms  {line}")
    print(f"BK4819: {sum(1 for x in bklog if x[1] == 'W')} writes, {sum(1 for x in bklog if x[1] == 'R')} reads")
    print(f"ran ~{st['n']} instructions, {st['ms']} simulated ms")
    if args.png:
        visible = [gram[y * 162 + x + X_OFFSET] for y in range(128) for x in range(128)]
        write_png(args.png, visible, 128, 128, 3)
        print(f"LCD image: {args.png}")
    if fl["log"]:
        progs = sorted({a & ~0x1F for k, a in fl["log"] if k == "program"})
        erases = [hex(a) for k, a in fl["log"] if k == "erase"]
        print(f"flash writes: records at {[hex(a) for a in progs][:8]}, erases {erases}")
    if args.flash_out:
        open(args.flash_out, "wb").write(flash)


if __name__ == "__main__":
    sys.exit(main())
