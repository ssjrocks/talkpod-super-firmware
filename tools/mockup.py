#!/usr/bin/env python3
"""UI mockups at the radio's real resolution (128 x 128) with the X11 misc-fixed bitmap fonts the
firmware can embed, so a mockup shows exactly what the LCD can draw. Writes docs/ui/mockups.png.

usage: mockup.py [out.png]
"""
import gzip
import sys

from PIL import Image, ImageDraw, ImageFont, PcfFontFile

FONT_DIR = "/usr/share/fonts/X11/misc"
AA_FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"   # big digits, 2-bit anti-aliased
W = H = 128
SCALE = 3

BG = (8, 10, 18)
PANEL = (26, 32, 48)
PANEL2 = (40, 48, 70)
TEXT = (235, 238, 245)
DIM = (125, 135, 155)
CYAN = (0, 190, 255)
AMBER = (255, 196, 40)
GREEN = (40, 210, 100)
RED = (235, 55, 55)
PURPLE = (170, 120, 255)
ORANGE = (255, 140, 30)

_fonts = {}


def font(name):
    if name not in _fonts:
        with gzip.open(f"{FONT_DIR}/{name}.pcf.gz", "rb") as fp:
            pcf = PcfFontFile.PcfFontFile(fp)
        glyphs = {ch: g for ch, g in enumerate(pcf.glyph) if g}
        ascent = max(-g[1][1] for g in glyphs.values())
        descent = max(g[1][3] for g in glyphs.values())
        width = max(g[0][0] for g in glyphs.values())
        _fonts[name] = (glyphs, ascent, ascent + descent, width)
    return _fonts[name]


class Screen:
    def __init__(self, title):
        self.title = title
        self.im = Image.new("RGB", (W, H), BG)
        self.d = ImageDraw.Draw(self.im)

    def rect(self, x, y, w, h, c):
        self.d.rectangle([x, y, x + w - 1, y + h - 1], fill=c)

    def rrect(self, x, y, w, h, c):
        """rectangle with 1-pixel cut corners, the cheapest 'rounded' look on a 128-px screen"""
        self.rect(x + 1, y, w - 2, h, c)
        self.rect(x, y + 1, w, h - 2, c)

    def text(self, x, y, s, f, c):
        glyphs, ascent, _, width = font(f)
        px = self.im.load()
        for ch in s:
            g = glyphs.get(ord(ch))
            if g:
                (left, top, _, _), bm = g[1], g[3]
                bp = bm.load()
                for gy in range(bm.size[1]):
                    for gx in range(bm.size[0]):
                        X, Y = x + left + gx, y + ascent + top + gy
                        if bp[gx, gy] and 0 <= X < W and 0 <= Y < H:
                            px[X, Y] = c
            x += width
        return x

    def aa(self, x, y, s, size, c):
        """anti-aliased text quantised to 4 levels, as the firmware would store it (2 bits/pixel)"""
        f = ImageFont.truetype(AA_FONT, size)
        l, t, r, b = f.getbbox(s)
        mask = Image.new("L", (r, b), 0)
        ImageDraw.Draw(mask).text((0, 0), s, font=f, fill=255)
        mp, px = mask.load(), self.im.load()
        for my in range(b):
            for mx in range(r):
                a = round(mp[mx, my] / 255 * 3) / 3
                X, Y = x + mx, y + my - t
                if a and 0 <= X < W and 0 <= Y < H:
                    bg = px[X, Y]
                    px[X, Y] = tuple(int(bg[i] + (c[i] - bg[i]) * a) for i in range(3))
        return x + r

    def width(self, s, f):
        return len(s) * font(f)[3]

    def ctext(self, y, s, f, c, x0=0, x1=W):
        self.text(x0 + (x1 - x0 - self.width(s, f)) // 2, y, s, f, c)

    def rtext(self, xr, y, s, f, c):
        self.text(xr - self.width(s, f), y, s, f, c)

    # small icons
    def bars(self, x, y, n, c, off=PANEL2):
        for i in range(5):
            h = 2 + i * 2
            self.rect(x + i * 3, y + 10 - h, 2, h, c if i < n else off)

    def battery(self, x, y, pct, c=GREEN):
        self.rect(x, y, 14, 8, DIM)
        self.rect(x + 1, y + 1, 12, 6, BG)
        self.rect(x + 14, y + 2, 2, 4, DIM)
        self.rect(x + 2, y + 2, max(1, 10 * pct // 100), 4, c)

    def envelope(self, x, y, c):
        self.rect(x, y, 11, 8, c)
        for i in range(5):
            self.im.putpixel((x + 1 + i, y + 1 + i // 1 if i < 4 else y + 4), BG)
            self.im.putpixel((x + 9 - i, y + 1 + i // 1 if i < 4 else y + 4), BG)

    def lock(self, x, y, c):
        self.rect(x + 1, y, 5, 1, c)
        self.rect(x + 1, y, 1, 4, c)
        self.rect(x + 5, y, 1, 4, c)
        self.rect(x, y + 3, 7, 5, c)

    def chip(self, x, y, s, fg, bg, f="5x8"):
        w = self.width(s, f) + 4
        self.rrect(x, y, w, 10, bg)
        self.text(x + 2, y + 1, s, f, fg)
        return x + w + 2

    def meter(self, x, y, w, level, segs=22):
        seg = w // segs
        for i in range(segs):
            on = i < level
            c = (GREEN if i < 12 else AMBER if i < 17 else RED) if on else PANEL2
            self.rect(x + i * seg, y, seg - 1, 5, c)

    def status(self, rx=False, tx=False, msgs=0, bat=86):
        self.rect(0, 0, W, 12, PANEL)
        if tx:
            self.chip(2, 1, "TX", TEXT, RED)
        elif rx:
            self.chip(2, 1, "RX", BG, GREEN)
        else:
            self.bars(3, 1, 0, GREEN)
        x = 22
        if msgs:
            self.envelope(x, 2, PURPLE)
            x = self.text(x + 13, 2, str(msgs), "5x8", PURPLE) + 3
        self.text(x, 2, "APRS", "5x8", CYAN)
        self.lock(80, 2, DIM)
        self.battery(110, 2, bat)
        self.rtext(108, 2, f"{bat}", "5x8", DIM)


def main_focus(rx):
    s = Screen("A  focus, receiving" if rx else "A  focus, idle")
    s.status(rx=rx, msgs=2)
    s.chip(3, 16, "CH 11", BG, CYAN, "6x10")
    s.text(42, 16, "CB Call", "8x13B", TEXT)
    x = s.aa(3, 33, "476.91", 25, AMBER)
    s.aa(x + 1, 33, "25", 14, AMBER)
    s.text(x + 2, 49, "MHz", "5x8", DIM)
    x = s.chip(3, 62, "FM", TEXT, PANEL2)
    x = s.chip(x, 62, "N", TEXT, PANEL2)
    x = s.chip(x, 62, "CT 91.5", TEXT, PANEL2)
    s.chip(x, 62, "5W", TEXT, PANEL2)
    s.text(3, 77, "S", "5x8", DIM)
    s.meter(10, 78, 112, 15 if rx else 2)
    for i, lab in enumerate(("1", "3", "5", "7", "9", "+20")):
        s.text(10 + i * 19, 85, lab, "4x6", DIM)
    s.rrect(2, 96, 124, 30, PANEL)
    if rx:
        s.text(6, 99, "VK2XYZ", "7x13B", GREEN)
        s.rtext(122, 101, "-71 dBm", "5x8", DIM)
        s.text(6, 114, "receiving 0:12", "5x8", DIM)
    else:
        s.text(6, 99, "Last: VK2XYZ", "6x10", TEXT)
        s.text(6, 112, "3 min ago, CH 11", "5x8", DIM)
    return s


def main_dual():
    s = Screen("B  dual watch")
    s.status(msgs=2)
    for i, (ch, name, f, active) in enumerate((("CH 11", "CB Call", "476.9125", True),
                                               ("CH 40", "Highway", "477.4000", False))):
        y = 15 + i * 50
        s.rrect(2, y, 124, 46, PANEL if active else BG)
        if not active:
            s.rect(2, y, 124, 1, PANEL)
        s.chip(5, y + 3, ch, BG if active else TEXT, CYAN if active else PANEL2, "6x10")
        s.text(46, y + 3, name, "7x13B" if active else "6x13", TEXT if active else DIM)
        x = s.aa(6, y + 18, f[:-2], 21, AMBER if active else DIM)
        s.aa(x + 1, y + 18, f[-2:], 12, AMBER if active else DIM)
        s.text(x + 2, y + 31, "MHz", "5x8", DIM)
        s.meter(8, y + 41, 112, 6 if active else 0, 16)
    s.text(4, 117, "A/B swap  MENU", "5x8", DIM)
    return s


def tx_screen():
    s = Screen("transmitting")
    s.status(tx=True, msgs=2)
    s.rect(0, 12, W, 2, RED)
    s.chip(3, 18, "CH 11", TEXT, RED, "6x10")
    s.text(42, 18, "CB Call", "8x13B", TEXT)
    x = s.aa(3, 35, "476.91", 25, TEXT)
    s.aa(x + 1, 35, "25", 14, TEXT)
    s.text(3, 66, "PWR", "5x8", DIM)
    s.meter(22, 67, 100, 18, 20)
    s.text(3, 78, "MIC", "5x8", DIM)
    s.meter(22, 79, 100, 9, 20)
    s.rrect(2, 96, 124, 30, (60, 16, 20))
    s.ctext(99, "TX  0:07", "9x15B", TEXT)
    s.ctext(116, "time-out 3:00", "5x8", (230, 160, 160))
    return s


def menu():
    s = Screen("menu")
    s.rect(0, 0, W, 14, PANEL)
    s.text(4, 2, "Menu", "7x13B", TEXT)
    s.rtext(124, 4, "2/8", "5x8", DIM)
    items = [("Channels", CYAN), ("Messages", PURPLE), ("APRS", CYAN), ("Pager", ORANGE),
             ("Scan list", GREEN), ("Radio", TEXT), ("Display", TEXT)]
    for i, (name, c) in enumerate(items):
        y = 17 + i * 15
        if i == 1:
            s.rrect(2, y - 1, 124, 15, PANEL2)
        s.rect(6, y + 3, 6, 6, c)
        s.text(17, y, name, "7x13" if i != 1 else "7x13B", TEXT if i == 1 else DIM if i > 4 else TEXT)
        if i == 1:
            s.rtext(122, y + 3, "2 new", "5x8", PURPLE)
    return s


def messages():
    s = Screen("messages (FSK)")
    s.rect(0, 0, W, 14, PANEL)
    s.text(4, 2, "Messages", "7x13B", TEXT)
    s.rtext(124, 4, "CH 11", "5x8", DIM)
    bubbles = [(False, "VK2XYZ", "on my way, 10 min"), (True, "", "ok, see you at camp"),
               (False, "VK2XYZ", "bring the spare ant")]
    y = 17
    for mine, who, msg in bubbles:
        lines, cur = [], ""
        for word in msg.split():
            if len(cur) + len(word) + (1 if cur else 0) > 17:
                lines.append(cur)
                cur = word
            else:
                cur = (cur + " " + word).strip()
        lines.append(cur)
        h = 4 + 9 * len(lines) + (9 if who else 0)
        x = 30 if mine else 2
        s.rrect(x, y, 96, h, CYAN if mine else PANEL2)
        ty = y + 2
        if who:
            s.text(x + 3, ty, who, "5x8", PURPLE)
            ty += 9
        for ln in lines:
            s.text(x + 3, ty, ln, "5x8", BG if mine else TEXT)
            ty += 9
        y += h + 4
    s.rrect(2, 114, 124, 13, PANEL)
    s.text(5, 116, "type: 2abc 3def...", "5x8", DIM)
    return s


def aprs():
    s = Screen("APRS heard")
    s.rect(0, 0, W, 14, PANEL)
    s.text(4, 2, "APRS", "7x13B", CYAN)
    s.rtext(124, 4, "145.175", "5x8", DIM)
    rows = [("VK2RHR-1", "digi", "12km NE", "1m"), ("VK2ABC-9", "car 64kmh", "3km W", "4m"),
            ("VK2XYZ-7", "hiking", "800m S", "9m"), ("VK2WI-10", "wx 21C", "25km N", "12m")]
    for i, (call, info, dist, age) in enumerate(rows):
        y = 17 + i * 27
        s.rrect(2, y, 124, 25, PANEL if i else PANEL2)
        s.text(5, y + 2, call, "6x10", TEXT)
        s.rtext(122, y + 3, age, "5x8", DIM)
        s.text(5, y + 14, info, "5x8", CYAN)
        s.rtext(122, y + 14, dist, "5x8", AMBER)
    return s


def pager():
    s = Screen("POCSAG pager")
    s.rect(0, 0, W, 14, PANEL)
    s.text(4, 2, "Pager", "7x13B", ORANGE)
    s.rtext(124, 4, "1200 bd", "5x8", DIM)
    pages = [("1234567", "14:02", "TEST PAGE FROM BENCH"), ("1234567", "13:47", "RADIO CHECK OK"),
             ("0042000", "13:10", "ALERT 3 PAGES")]
    for i, (ric, t, msg) in enumerate(pages):
        y = 17 + i * 36
        s.rrect(2, y, 124, 34, PANEL2 if i == 0 else PANEL)
        s.text(5, y + 2, "RIC " + ric, "5x8", ORANGE)
        s.rtext(122, y + 2, t, "5x8", DIM)
        lines = [msg[j:j + 20] for j in range(0, len(msg), 20)]
        for k, ln in enumerate(lines[:2]):
            s.text(5, y + 12 + k * 10, ln, "6x10", TEXT)
    return s


def boot():
    s = Screen("boot splash (GIF)")
    for y in range(H):
        for x in range(W):
            t = (x + y) / (W + H)
            s.im.putpixel((x, y), (int(10 + 30 * t), int(20 + 80 * t), int(60 + 120 * t)))
    s.ctext(46, "your GIF", "9x15B", TEXT)
    s.ctext(64, "plays once", "6x10", TEXT)
    s.ctext(112, "A36SE CFW 1.0", "5x8", DIM)
    return s


def sheet(screens, out):
    cols = 4
    rows = (len(screens) + cols - 1) // cols
    cw, ch = W * SCALE + 24, H * SCALE + 44
    img = Image.new("RGB", (cols * cw + 24, rows * ch + 24), (225, 228, 233))
    d = ImageDraw.Draw(img)
    for i, s in enumerate(screens):
        x, y = 24 + (i % cols) * cw, 24 + (i // cols) * ch
        d.rectangle([x - 6, y - 6, x + W * SCALE + 5, y + H * SCALE + 5], fill=(20, 22, 26))
        img.paste(s.im.resize((W * SCALE, H * SCALE), Image.NEAREST), (x, y))
        d.text((x, y + H * SCALE + 12), s.title, fill=(30, 30, 30))
    img.save(out)


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "docs/ui/mockups.png"
    sheet([main_focus(False), main_focus(True), main_dual(), tx_screen(),
           menu(), messages(), aprs(), pager()], out)
    print(out)
