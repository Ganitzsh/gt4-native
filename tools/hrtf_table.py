#!/usr/bin/env python3
"""The HRTF table for D9's audio.spatial = hrtf (docs/modern/spikes/D9-voices-to-entrants.md), made offline from MIT's
KEMAR measurements and committed as platform/audio/spatial/hrtf_kemar.h. Not part of any build.

    py -I tools/hrtf_table.py [--zip PATH]      (without --zip: downloads it into build/hrtf/)

The input is pinned: MIT KEMAR's compact set, compact.zip from sound.media.mit.edu (SHA-256 below); another file is
refused, and the archive itself is never committed. From its horizontal plane (elev0: azimuths 0..180 by 5 degrees,
128-point HRIRs, 44.1 kHz) the tool:
- takes the compact set's own pairs: 128-point symmetrical HRTFs derived from KEMAR's left ear (the normal DB-061
  pinna), equalized for the Optimus Pro 7 speaker; a file H0eAAAa holds the left and right ear for a source AAA
  degrees to the right (its right ear is the left ear's response at 360 - AAA, so the two ears come from one
  measured ear, mirrored). The left half (185..355) is the right half with the ears swapped;
- resamples each response to 48 kHz with a Blackman-windowed sinc (32 zero crossings each side, cutoff at 0.95 of
  22.05 kHz), 140 taps (128 x 48000 / 44100, rounded up);
- scales them all by one factor so that the frontal pair (azimuth 0) has unit energy per ear on average, and
  writes them as int16 with that common scale (kScale: the float a sample stands for is value * kScale).
The output depends on Python's math library (sin, cos) only: the same Python on any machine gives the same bytes.

The data: Bill Gardner and Keith Martin, "HRTF Measurements of a KEMAR Dummy-Head Microphone", MIT Media Lab
Perceptual Computing Technical Report #280, 1994. Copyright 1994 by the MIT Media Laboratory; "provided free with no
restrictions on use, provided the authors are cited when the data is used in any research or commercial application"
(the archive's README). CREDITS.md and THIRD_PARTY_NOTICES.md cite it.
"""
import argparse
import hashlib
import io
import math
import os
import struct
import sys
import urllib.request
import wave
import zipfile

URL = "https://sound.media.mit.edu/resources/KEMAR/compact.zip"
SHA256 = "0bcd69f8e8760cf8eacff4a89594ceca7d44fb94f28ecf0bf19970003bbfb3e0"
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "platform", "audio", "spatial", "hrtf_kemar.h")
RATE_IN, RATE_OUT, TAPS_IN = 44100, 48000, 128
TAPS = -(-TAPS_IN * RATE_OUT // RATE_IN)   # 140
ZEROS, CUTOFF = 32, 0.95
STEP = 5


def fetch(path):
    if path:
        data = open(path, "rb").read()
    else:
        cache = os.path.join(ROOT, "build", "hrtf", "compact.zip")
        if not os.path.isfile(cache):
            os.makedirs(os.path.dirname(cache), exist_ok=True)
            with urllib.request.urlopen(URL, timeout=60) as r:
                open(cache, "wb").write(r.read())
        data = open(cache, "rb").read()
    got = hashlib.sha256(data).hexdigest()
    if got != SHA256:
        raise SystemExit(f"not the pinned KEMAR compact set: SHA-256 {got}, expected {SHA256}")
    return data


def read_pair(z, azimuth):
    w = wave.open(io.BytesIO(z.read(f"elev0/H0e{azimuth:03d}a.wav")))
    if (w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()) != (2, 2, RATE_IN, TAPS_IN):
        raise SystemExit(f"H0e{azimuth:03d}a.wav: not 2 x 16-bit x {RATE_IN} Hz x {TAPS_IN}")
    frames = struct.unpack(f"<{2 * TAPS_IN}h", w.readframes(TAPS_IN))
    return [float(v) for v in frames[0::2]], [float(v) for v in frames[1::2]]


def resample(x):
    """x (44.1 kHz) at 48 kHz, TAPS samples: a Blackman-windowed sinc, cutoff CUTOFF of the input's Nyquist."""
    out = []
    ratio = RATE_IN / RATE_OUT
    half = ZEROS / CUTOFF
    for n in range(TAPS):
        t = n * ratio   # the output sample's time, in input samples
        acc = 0.0
        for k in range(max(0, math.floor(t - half)), min(len(x), math.ceil(t + half) + 1)):
            u = t - k
            if abs(u) >= half:
                continue
            arg = CUTOFF * u
            sinc = 1.0 if arg == 0 else math.sin(math.pi * arg) / (math.pi * arg)
            w = 0.42 + 0.5 * math.cos(math.pi * u / half) + 0.08 * math.cos(2 * math.pi * u / half)
            acc += x[k] * CUTOFF * sinc * w
        out.append(acc)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--zip", help="a local copy of compact.zip (checked against the pin)")
    a = ap.parse_args()
    z = zipfile.ZipFile(io.BytesIO(fetch(a.zip)))
    right = {az: read_pair(z, az) for az in range(0, 181, STEP)}
    table = []
    for az in range(0, 360, STEP):
        if az <= 180:
            left, rght = right[az]
        else:   # a source on the left: the mirror of 360 - az, ears swapped
            rght, left = right[360 - az]
        table.append((resample(left), resample(rght)))
    front = table[0]
    energy = (sum(v * v for v in front[0]) + sum(v * v for v in front[1])) / 2
    unit = 1.0 / math.sqrt(energy)   # the frontal pair at unit energy per ear
    peak = max(abs(v) * unit for pair in table for ear in pair for v in ear)
    scale = peak / 32767.0   # the float a sample of 1 stands for
    def ear_lines(values):
        q = [max(-32768, min(32767, round(v * unit / scale))) for v in values]
        return ["            " + ", ".join(f"{v:6d}" for v in q[i:i + 12]) + "," for i in range(0, len(q), 12)]
    body = []
    for az, (left, rght) in zip(range(0, 360, STEP), table):
        body.append(f"    {{   // {az} degrees")
        for name, ear in (("left", left), ("right", rght)):
            body += [f"        {{   // {name}"] + ear_lines(ear) + ["        },"]
        body.append("    },")
    header = (
        "#pragma once\n"
        "// Generated by tools/hrtf_table.py: do not edit. HRIRs on the horizontal plane for D9's\n"
        "// audio.spatial = hrtf.\n"
        "// Source: MIT KEMAR's compact set: 128-point symmetrical HRTFs from KEMAR's left ear (the normal DB-061\n"
        "// pinna), speaker-equalized; the right ear is the left ear's mirror response. compact.zip SHA-256\n"
        f"// {SHA256}.\n"
        "// Bill Gardner and Keith Martin, \"HRTF Measurements of a KEMAR Dummy-Head Microphone\", MIT Media Lab\n"
        "// Perceptual Computing Technical Report #280, 1994; free with no restrictions on use, the authors cited\n"
        "// (CREDITS.md, THIRD_PARTY_NOTICES.md).\n"
        f"// Resampled from {RATE_IN} to {RATE_OUT} Hz (a Blackman-windowed sinc, {ZEROS} zero crossings,\n"
        f"// cutoff {CUTOFF} of Nyquist), {TAPS} taps. Azimuth 0 is ahead, then clockwise (to the right) by\n"
        f"// {STEP} degrees. The frontal pair has unit energy per ear; a value v stands for v * kHrtfScale.\n"
        "#include <cstdint>\n\n"
        "namespace gt4::spatial {\n\n"
        f"constexpr int kHrtfAzimuths = {360 // STEP}, kHrtfStepDegrees = {STEP}, kHrtfTaps = {TAPS};\n"
        f"constexpr float kHrtfScale = {scale!r}f;\n"
        "// [azimuth][ear: 0 left, 1 right][tap]\n"
        "constexpr int16_t kHrtf[kHrtfAzimuths][2][kHrtfTaps] = {\n")
    text = header + "\n".join(body) + "\n};\n\n}  // namespace gt4::spatial\n"
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print(f"{OUT}: {360 // STEP} azimuths x 2 ears x {TAPS} taps, scale {scale!r}; "
          f"SHA-256 {hashlib.sha256(text.encode()).hexdigest()}")


if __name__ == "__main__":
    sys.exit(main())
