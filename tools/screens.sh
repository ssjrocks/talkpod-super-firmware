#!/usr/bin/env bash
# Renders docs/ui/screens.png, the README's picture: real screens from build/a36se-fw.elf in the
# emulator. It uses the test flash image only, so nothing from a real radio ends up in the picture.
#
# Usage: tools/screens.sh
set -euo pipefail
cd "$(dirname "$0")/.."
flash=tests/data/flash-test.bin
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT

shot() {    # shot NAME EMU_ARGS...
    local name=$1
    shift
    .venv/bin/python tools/emu.py build/a36se-fw.elf --adc 0x0A20 "$@" --png "$out/$name.png" > /dev/null 2>&1
    [[ -s "$out/$name.png" ]] || { echo "no picture for $name" >&2; exit 1; }
}

msg() {     # a UV-K5 message as it arrives
    python3 -c "import sys; print((bytes([100]) + sys.argv[1].encode().ljust(30, b'\0') + bytes(13)).hex())" "$1"
}

shot main --flash "$flash" --ms 1500
shot rx --flash "$flash" --ms 3000 --signal-at 47667500 --bkreg 0x67=0x00B2
shot tx --flash "$flash" --ms 2500 --keys 1500:PTT:1500
shot menu --flash "$flash" --ms 1200 --keys 700:MENU:60
shot fs-setup --flash "$flash" --ms 1900 --keys 700:MENU:60,1000:5:60,1300:MENU:60

# the scanner on 121.5 to 121.9 MHz, stopped on a signal in AM
range="700:MENU:60,1000:5:60,1300:MENU:60,1700:1:50,1830:2:50,1960:1:50,2090:5:50,2220:0:50,2350:0:50,2480:0:50"
range+=",2710:1:50,2840:2:50,2970:1:50,3100:9:50,3230:0:50,3360:0:50,3490:0:50,3720:MENU:60,3900:MENU:60"
shot fs-run --flash "$flash" --ms 5400 --keys "$range" --signal-at 12160000 --bkreg 0x67=0x00A0

shot settings --flash "$flash" --ms 1600 --keys 600:MENU:60,1000:1:60
shot settings2 --flash "$flash" --ms 1600 --keys 600:MENU:60,1000:2:60

shot ssb --flash "$flash" --ms 3000 --keys 800:MENU:800,2000:FM:80,2500:FM:80
shot messages --flash "$flash" --ms 3800 --keys 500:5:800 --fsk-rx "2000:$(msg 'on my way, 10 min')"
shot panel --flash "$flash" --ms 4000 --keys 1500:8:900,2700:A/B:900
shot tunes --flash "$flash" --ms 3200 --keys 1500:7:900,2700:DOWN:40
shot pager --flash "$flash" --ms 4000 --keys 1500:8:900 \
    --fsk-rx "2500:$(.venv/bin/python tools/pocsag_emu.py rx 1234567 'TEST PAGE FROM THE BENCH')" \
    --fsk-rx "3000:$(.venv/bin/python tools/pocsag_emu.py rx 1234567 'RADIO CHECK OK')"
# Morse: PARIS typed on the keypad and going out; then, after a sent text, holding FM recalls it
paris="500:9:900,2000:7:60,2300:2:60,2600:7:60,2750:7:60,2900:7:60,3200:4:60,3350:4:60,3500:4:60"
paris+=",3800:7:60,3950:7:60,4100:7:60,4250:7:60,5600:MENU:60"
shot morse --flash "$flash" --ms 7300 --keys "$paris"
shot history --flash "$flash" --ms 9600 --keys "$paris,8400:FM:900"
shot about --flash "$flash" --ms 1700 --keys 700:MENU:60,1000:UP:60,1300:MENU:60
python3 tools/splash.py docs/ui/test-splash.gif --image "$flash" "$out/flash-splash.bin" > /dev/null
shot splash --flash "$out/flash-splash.bin" --ms 900

python3 - "$out" <<'EOF'
import sys

from PIL import Image, ImageDraw, ImageFont

SCREENS = [("main", "dual watch"), ("rx", "receiving"), ("tx", "transmitting"), ("menu", "menu: categories, digits open them"),
           ("fs-setup", "frequency scanner: range"), ("fs-run", "frequency scanner: stopped on a signal, AM"),
           ("settings", "1 Radio"), ("settings2", "2 Transmit: Roger, tail cut..."),
           ("ssb", "SSB receive on a VFO"), ("messages", "text messages"),
           ("panel", "hold A/B on the Pager: its settings"), ("tunes", "tunes: MENU plays, hold MENU sends"), ("pager", "POCSAG pager"), ("morse", "Morse code going out"), ("history", "hold FM: a text sent before"),
           ("splash", "boot splash"), ("about", "about")]
SIZE, GAP, LABEL = 384, 24, 34
font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 15)
rows = (len(SCREENS) + 3) // 4
sheet = Image.new("RGB", (GAP + (SIZE + GAP) * 4, GAP + (SIZE + LABEL) * rows), (225, 228, 233))
draw = ImageDraw.Draw(sheet)
for i, (name, label) in enumerate(SCREENS):
    x, y = GAP + (SIZE + GAP) * (i % 4), GAP + (SIZE + LABEL) * (i // 4)
    sheet.paste(Image.open(f"{sys.argv[1]}/{name}.png").convert("RGB"), (x, y))
    draw.text((x + 2, y + SIZE + 7), label, font=font, fill=(50, 54, 62))
sheet.save("docs/ui/screens.png", optimize=True)
EOF
echo "wrote docs/ui/screens.png"
