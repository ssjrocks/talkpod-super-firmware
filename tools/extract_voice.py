#!/usr/bin/env python3
"""Extract the stock voice prompts from a flash backup as WAV files.

usage: extract_voice.py BACKUP.bin OUT_DIR

Layout (stock firmware, docs/stock-firmware.md): an index at 0x110000 of (u32 length, u32 offset)
pairs, audio at offset + 0x90000. Stock numbers the prompts from 16 (its table base is 0x10FF80).
Audio is standard IMA ADPCM (89 steps, 16-bit predictor) with the high nibble first, state reset
per prompt: on this radio's data that decodes with no clipping and every prompt ending at zero.
(The Super 1.22 decoder at 0x800d4f0 clamps the index to 58 and the predictor to 12 bits, which
clips this data.) Super plays prompts through TMR1 at 108 MHz / 2201 / 6 = 8178 samples per second;
the factory firmware's rate is assumed to be the same.

Writes prompt_NNN.wav (stock number) for each prompt, all_prompts.wav with 0.4 s of silence between
them, and all_prompts.txt listing where each one starts.
"""
import os
import struct
import sys
import wave

INDEX, DATA_BASE, FIRST = 0x110000, 0x90000, 16
RATE = round(108_000_000 / 2201 / 6)

STEPS = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66,
         73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408,
         449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
         2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630,
         9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
         32767]
INDEX_ADJ = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8]


def decode(data):
    pred, idx, out = 0, 0, []
    for byte in data:
        for code in (byte >> 4, byte & 15):
            step = STEPS[idx]
            idx = min(max(idx + INDEX_ADJ[code], 0), 88)
            diff = step >> 3
            if code & 4:
                diff += step
            if code & 2:
                diff += step >> 1
            if code & 1:
                diff += step >> 2
            pred = max(pred - diff, -32768) if code & 8 else min(pred + diff, 32767)
            out.append(pred)
    return out


def write_wav(path, samples):
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b"".join(struct.pack("<h", s) for s in samples))


def main():
    flash = open(sys.argv[1], "rb").read()
    out = sys.argv[2]
    os.makedirs(out, exist_ok=True)
    all_samples, listing = [], []
    for n in range(256):
        length, off = struct.unpack_from("<II", flash, INDEX + n * 8)
        if length in (0, 0xFFFFFFFF) or off + DATA_BASE + length > len(flash):
            break
        samples = decode(flash[off + DATA_BASE:off + DATA_BASE + length])
        number = FIRST + n
        write_wav(os.path.join(out, f"prompt_{number:03d}.wav"), samples)
        listing.append(f"{len(all_samples) / RATE:7.2f} s  prompt {number:3d}  ({len(samples) / RATE:.2f} s)")
        all_samples += samples + [0] * int(RATE * 0.4)
    write_wav(os.path.join(out, "all_prompts.wav"), all_samples)
    open(os.path.join(out, "all_prompts.txt"), "w").write("\n".join(listing) + "\n")
    print(f"{len(listing)} prompts at {RATE} Hz, {len(all_samples) / RATE:.0f} s in all_prompts.wav")


if __name__ == "__main__":
    main()
