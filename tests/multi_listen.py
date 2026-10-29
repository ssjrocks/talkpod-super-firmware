#!/usr/bin/env python3
"""Listen for serial data from bare_passthru firmware at multiple baud rates.

The firmware sends at several rates sequentially:
  Phase 1: bootloader's rate (probably 57600), prefix "1:"
  Phase 2: bootloader's BRR (57600 @ 108MHz), prefix "2:"
  Phase 3: 57600 @ 8MHz (BRR=139), prefix "3:"
  Phase 4: 9600 @ 8MHz (BRR=833), prefix "4:"
  Main loop: repeats at bootloader's rate

This script listens at multiple baud rates for a few seconds each,
reporting any received data. Run it right after flashing.
"""

import serial
import sys
import time

PORT = '/dev/ttyUSB0'
RATES = [57600, 115200, 9600, 4800, 2400, 19200, 38400, 4267, 8533, 711, 1200, 300]

def listen_at(port, baud, duration=3.0):
    """Listen at a specific baud rate for duration seconds."""
    try:
        ser = serial.Serial(port, baud, timeout=0.1)
        ser.reset_input_buffer()
        start = time.time()
        data = bytearray()
        while time.time() - start < duration:
            chunk = ser.read(256)
            if chunk:
                data.extend(chunk)
        ser.close()
        return bytes(data)
    except Exception as e:
        return f"ERROR: {e}"

def main():
    port = sys.argv[1] if len(sys.argv) > 1 else PORT

    print(f"Listening on {port} at multiple baud rates...")
    print(f"Flash the firmware, then this script will cycle through rates.\n")

    for baud in RATES:
        print(f"  {baud:>6} baud: ", end='', flush=True)
        data = listen_at(port, baud, duration=2.5)
        if isinstance(data, str):
            print(data)
            continue
        if not data:
            print("(no data)")
            continue

        # Show hex and try to decode as ASCII
        hex_str = data[:32].hex(' ')
        try:
            ascii_str = data.decode('ascii', errors='replace')[:64]
            ascii_str = repr(ascii_str)
        except:
            ascii_str = ""

        print(f"{len(data)} bytes: {hex_str}")
        if ascii_str:
            print(f"         ASCII: {ascii_str}")

        # Check for "ALIVE" in the data
        if b'ALIVE' in data or b'alive' in data:
            print(f"\n  *** FOUND 'ALIVE' at {baud} baud! ***\n")

    print("\nDone.")

if __name__ == '__main__':
    main()
