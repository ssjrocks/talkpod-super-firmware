#!/usr/bin/env python3
"""Continuous serial listener. Prints hex + ASCII for any received data."""
import serial, sys, time

port = sys.argv[1] if len(sys.argv) > 1 else '/dev/ttyUSB0'
baud = int(sys.argv[2]) if len(sys.argv) > 2 else 57600

print(f"Listening on {port} @ {baud}...")
ser = serial.Serial(port, baud, timeout=0.5)
ser.reset_input_buffer()

try:
    while True:
        data = ser.read(256)
        if data:
            ts = time.strftime('%H:%M:%S')
            hexd = data.hex(' ')
            try:
                asc = data.decode('ascii', errors='replace')
                asc = repr(asc)[1:-1]
            except:
                asc = ''
            print(f"[{ts}] ({len(data):3d}) {hexd}")
            if asc:
                print(f"         ASCII: {asc}")
            if b'ALIVE' in data:
                print("  >>> FOUND ALIVE! <<<")
except KeyboardInterrupt:
    pass
finally:
    ser.close()
    print("\nDone.")
