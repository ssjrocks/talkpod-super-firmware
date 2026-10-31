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
shot fs-setup --flash "$flash" --ms 1500 --keys 700:MENU:60,950:MENU:60

# the scanner on 121.5 to 121.9 MHz, stopped on a signal in AM
range="700:MENU:60,950:MENU:60,1300:1:50,1430:2:50,1560:1:50,1690:5:50,1820:0:50,1950:0:50,2080:0:50"
range+=",2310:1:50,2440:2:50,2570:1:50,2700:9:50,2830:0:50,2960:0:50,3090:0:50,3320:MENU:60"
shot fs-run --flash "$flash" --ms 5000 --keys "$range" --signal-at 12160000 --bkreg 0x67=0x00A0

keys="600:MENU:60"
t=800
for _ in $(seq 1 30); do
    keys+=",$t:DOWN:40"
    t=$((t + 120))
done
shot settings --flash "$flash" --ms $((t + 300)) --keys "$keys"

shot ssb --flash "$flash" --ms 3000 --keys 800:MENU:800,2000:FM:80,2500:FM:80
shot messages --flash "$flash" --ms 3800 --keys 500:5:800 --fsk-rx "2000:$(msg 'on my way, 10 min')"
shot aprs --flash "$flash" --ms 4300 --keys 1500:7:900 \
    --fsk-rx "2600:$(.venv/bin/python tools/aprs_emu.py rx N0CALL-9 APDR16 '!3351.20S/15112.40E>Mobile 146.5')" \
    --fsk-rx "3300:$(.venv/bin/python tools/aprs_emu.py rx N0CALL-7 APK004 '!3340.00S/15100.00E>Hiking')"
shot pager --flash "$flash" --ms 4000 --keys 1500:8:900 \
    --fsk-rx "2500:$(.venv/bin/python tools/pocsag_emu.py rx 1234567 'TEST PAGE FROM THE BENCH')" \
    --fsk-rx "3000:$(.venv/bin/python tools/pocsag_emu.py rx 1234567 'RADIO CHECK OK')"
python3 tools/splash.py docs/ui/test-splash.gif --image "$flash" "$out/flash-splash.bin" > /dev/null
shot splash --flash "$out/flash-splash.bin" --ms 900

python3 - "$out" <<'EOF'
import sys

from PIL import Image, ImageDraw, ImageFont

SCREENS = [("main", "dual watch"), ("rx", "receiving"), ("tx", "transmitting"), ("menu", "menu"),
           ("fs-setup", "frequency scanner: range"), ("fs-run", "frequency scanner: stopped on a signal, AM"),
           ("settings", "settings, stock's included"), ("ssb", "SSB receive on a VFO"),
           ("messages", "text messages"), ("aprs", "APRS heard"), ("pager", "POCSAG pager"),
           ("splash", "boot splash")]
SIZE, GAP, LABEL = 384, 24, 34
font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 15)
sheet = Image.new("RGB", (GAP + (SIZE + GAP) * 4, GAP + (SIZE + LABEL) * 3), (225, 228, 233))
draw = ImageDraw.Draw(sheet)
for i, (name, label) in enumerate(SCREENS):
    x, y = GAP + (SIZE + GAP) * (i % 4), GAP + (SIZE + LABEL) * (i // 4)
    sheet.paste(Image.open(f"{sys.argv[1]}/{name}.png").convert("RGB"), (x, y))
    draw.text((x + 2, y + SIZE + 7), label, font=font, fill=(50, 54, 62))
sheet.save("docs/ui/screens.png", optimize=True)
EOF
echo "wrote docs/ui/screens.png"
