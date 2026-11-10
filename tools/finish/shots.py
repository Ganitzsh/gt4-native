#!/usr/bin/env python3
"""shots.py NAME CAP FRAMES SHOTS [--set k=v ...] [--env K=V ...] [--host EXE] [--size WxH]: replay, capture PNGs.

Local stills only (game content). Output: C:/Users/julie/gt4-wt-land/build/shots/NAME/*.png, and the host's summary
lines containing any of: Hybrid, validation, error, n3, F2b.
"""
import os
import struct
import subprocess
import sys
import zlib
from pathlib import Path

WT = Path(r"C:\Users\julie\gt4-wt-land")
CAPS = Path(r"C:\Users\julie\gt4-native-game\build\caps")


def png(ppm, path):
    data = ppm.read_bytes()
    tokens, at = [], 0
    while len(tokens) < 4:
        while data[at:at + 1].isspace():
            at += 1
        end = at
        while not data[end:end + 1].isspace():
            end += 1
        tokens.append(data[at:end])
        at = end
    w, h, pix = int(tokens[1]), int(tokens[2]), data[at + 1:]
    raw = b"".join(b"\x00" + pix[y * w * 3:(y + 1) * w * 3] for y in range(h))

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def main():
    a = sys.argv[1:]
    name, cap, frames, shots = a[:4]
    sets, envs, host, size = [], {}, WT / "build/host-windows-Release/gt4_host.exe", "1280x720"
    i = 4
    while i < len(a):
        if a[i] == "--set":
            sets += ["--set", a[i + 1]]
        elif a[i] == "--env":
            k, v = a[i + 1].split("=", 1)
            envs[k] = v
        elif a[i] == "--host":
            host = Path(a[i + 1])
        elif a[i] == "--size":
            size = a[i + 1]
        i += 2
    sys.path.insert(0, r"C:\Users\julie\gt4-native-game\tools")
    import slot
    slot.kill_with_me()
    d = WT / "build/shots" / name
    d.mkdir(parents=True, exist_ok=True)
    for old in d.glob("*.p*"):
        old.unlink()
    r = subprocess.run([str(host), "--headless", "--replay", str(CAPS / cap), "--frames", frames, "--fixed-step", "--size",
                        size, "--preset", "original", "--set", "display.frame_interpolation=off", "--set",
                        "texture.hd_pack=off", *sets, "--capture-frames", shots, "--capture-dir", str(d)],
                       capture_output=True, text=True, errors="replace", creationflags=subprocess.CREATE_NO_WINDOW,
                       env=dict(os.environ, **envs))
    got = sorted(d.glob("*.ppm"))
    for p in got:
        png(p, p.with_suffix(".png"))
        p.unlink()
    print(f"{name}: exit {r.returncode}, {len(got)} frames in {d}", flush=True)
    for line in (r.stdout + r.stderr).splitlines():
        low = line.lower()
        if any(k in low for k in ("hybrid", "validation", "error", "n3 items", "f2b glow x", "vuid", "night side", "n3 defer", "n3 baked", "headlight dbg")):
            print("   " + line[:400], flush=True)


if __name__ == "__main__":
    main()


