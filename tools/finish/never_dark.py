#!/usr/bin/env python3
"""never_dark.py CAP FRAMES SHOTS [--env K=V ...] [--tag NAME]: N3's never-dark measure on a night lap.

Three replays of the same frames at 1280x720 (hybrid, lighting.night as named):
  gt4   lighting.night = original: GT4's picture, its baked lamp light on the road;
  ours  lighting.night = enhanced, GT4_NIGHT_SIDE=1, GT4_N3_VERDICTS=1 (+ --env): our lamps, the baked light skipped;
  mask  the same with GT4_LIGHT_VIEW=4: where the skip took baked light away.
Per frame, over the masked pixels (HUD rows left out): the mean linear luminance of gt4 and of ours, and their
ratio; then over all frames the ratio's distribution, per frame and per 32-pixel block (the darkest and brightest
patches: 'never dark' and 'never twice' are both sides). Also the same ratio outside the mask on geometry our lamps
changed (what else we lit). Prints a table; keeps PNGs of the three worst frames each side in build/never_dark/TAG.
"""
import os
import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np

WT = Path(r"C:\Users\julie\gt4-wt-land")
CAPS = Path(r"C:\Users\julie\gt4-native-game\build\caps")
HOST = WT / "build/host-windows-Release/gt4_host.exe"


def replay(d, cap, frames, shots, night, envs):
    shutil.rmtree(d, ignore_errors=True)
    d.mkdir(parents=True)
    r = subprocess.run([str(HOST), "--headless", "--replay", str(CAPS / cap), "--frames", frames, "--fixed-step", "--size",
                        "1280x720", "--preset", "original", "--set", "display.frame_interpolation=off", "--set",
                        "texture.hd_pack=off", "--set", "render.scene=hybrid", "--set", f"lighting.night={night}",
                        "--capture-frames", shots, "--capture-dir", str(d)],
                       capture_output=True, text=True, errors="replace", creationflags=subprocess.CREATE_NO_WINDOW,
                       env=dict(os.environ, **envs))
    if r.returncode:
        print((r.stdout + r.stderr)[-2000:])
        raise SystemExit(f"replay failed: {d.name}")


def load(p):
    data = p.read_bytes()
    tokens, at = [], 0
    while len(tokens) < 4:
        while data[at:at + 1].isspace():
            at += 1
        end = at
        while not data[end:end + 1].isspace():
            end += 1
        tokens.append(data[at:end])
        at = end
    w, h = int(tokens[1]), int(tokens[2])
    return np.frombuffer(data, dtype=np.uint8, count=w * h * 3, offset=at + 1).reshape(h, w, 3)


def lum(img):
    lin = (img.astype(np.float32) / 255.0) ** 2.2
    return lin @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)


def save_png(img, path):
    import struct
    import zlib
    h, w, _ = img.shape
    raw = b"".join(b"\x00" + img[y].tobytes() for y in range(h))

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def main():
    a = sys.argv[1:]
    cap, frames, shots = a[:3]
    envs, tag = {}, "run"
    i = 3
    while i < len(a):
        if a[i] == "--env":
            k, v = a[i + 1].split("=", 1)
            envs[k] = v
        elif a[i] == "--tag":
            tag = a[i + 1]
        i += 2
    sys.path.insert(0, r"C:\Users\julie\gt4-native-game\tools")
    import slot
    slot.kill_with_me()
    out = WT / "build/never_dark" / tag
    ours_env = dict(GT4_NIGHT_SIDE="1", GT4_N3_VERDICTS="1", **envs)
    replay(out / "gt4", cap, frames, shots, "original", {})
    replay(out / "ours", cap, frames, shots, "enhanced", ours_env)
    mask_env = {k: v for k, v in ours_env.items() if k != "GT4_N3_DEFER"}   # (with it, binding 9 is no mask)
    replay(out / "mask", cap, frames, shots, "enhanced", dict(mask_env, GT4_LIGHT_VIEW="4"))
    rows, blocks = [], []
    for p in sorted((out / "gt4").glob("*.ppm")):
        g, o, m = load(p), load(out / "ours" / p.name), load(out / "mask" / p.name)
        h, w, _ = g.shape
        x0, x1 = w * 160 // 1280, w * 1120 // 1280          # the 4:3 picture inside 16:9
        sel = np.zeros((h, w), dtype=bool)
        sel[h * 28 // 100:h * 86 // 100, x0:x1] = True       # below the top HUD, above the bottom bar
        mask = (m[:, :, 0] > 200) & (m[:, :, 1] > 200) & (m[:, :, 2] > 200) & sel
        lg, lo = lum(g), lum(o)
        n = int(mask.sum())
        if n < 2000:
            continue
        rg, ro = float(lg[mask].mean()), float(lo[mask].mean())
        other = sel & ~mask & (np.abs(lo - lg) > 0.002)
        og, oo = (float(lg[other].mean()), float(lo[other].mean())) if other.sum() > 2000 else (0.0, 0.0)
        rows.append((p.stem, n, rg, ro, ro / max(rg, 1e-6), int(other.sum()), og, oo))
        for by in range(0, h, 32):
            for bx in range(x0, x1, 32):
                mb = mask[by:by + 32, bx:bx + 32]
                if mb.sum() >= 512:
                    blocks.append(float(lo[by:by + 32, bx:bx + 32][mb].mean()) /
                                  max(float(lg[by:by + 32, bx:bx + 32][mb].mean()), 1e-6))
    print(f"{tag}: {len(rows)} frames with a masked area; env {envs}")
    print("frame          masked px   gt4 lum   ours lum  ours/gt4 | elsewhere changed px  gt4     ours")
    for r in rows:
        print(f"{r[0]:14s} {r[1]:9d}  {r[2]:8.4f}  {r[3]:8.4f}  {r[4]:7.2f} | {r[5]:9d}  {r[6]:7.4f} {r[7]:7.4f}")
    if rows:
        ratios = np.array([r[4] for r in rows])
        b = np.array(blocks)
        print(f"frames: ours/gt4 under the mask: median {np.median(ratios):.2f}, min {ratios.min():.2f}, max {ratios.max():.2f}")
        print(f"blocks (32 px, {len(b)}): median {np.median(b):.2f}, p05 {np.percentile(b, 5):.2f}, p95 {np.percentile(b, 95):.2f}; "
              f"darker than half of GT4's {100 * (b < 0.5).mean():.1f} %, over twice GT4's {100 * (b > 2).mean():.1f} %")
        order = sorted(rows, key=lambda r: r[4])
        for r in order[:2] + order[-2:]:
            for kind in ("gt4", "ours"):
                save_png(load(out / kind / (r[0] + ".ppm")), out / f"{r[0]}_{kind}.png")
    for kind in ("gt4", "ours", "mask"):
        shutil.rmtree(out / kind, ignore_errors=True)


if __name__ == "__main__":
    main()
