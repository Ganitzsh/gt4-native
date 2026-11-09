#!/usr/bin/env python3
"""n3_dark.py TAG CAP FRAMES [--every N] [--from F]: N3's 'never dark' check for kFrameNight's choice (the architect,
2026-11-09). Three replays of CAP on the debug host (gt4_host_n3orient.exe: GT4_N3_V4SKIP judges v4 per item and skips
the texture; F2b's lamps at night enhanced), 640 x 360, a frame every N (default 50) from F (default 600):
  A  night original: the GS path, GT4's baked pools
  B  night enhanced with v4's skip: what N3 shows (pools skipped, F2b's lamps lighting the road)
  C  night enhanced without the skip
The skipped patches are where B and C differ. Inside them, B against A (Rec.709 luminance, 0-255): the mean change and
the share of pixels darker by more than 4 levels; the frames with the most darkening are imaged (A | B | the patches,
darker in red, lighter in green) in build/sessions/n3dark_TAG/. F2b's lamp line and v4's skip count are printed.
Pass: no patch darker than the GS path. One heavy slot per replay."""
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, "C:/Users/julie/gt4-lane-F/tools")
from r5_session import take_slot, log, CLONE  # noqa: E402
import frame_metrics as fm  # noqa: E402


def opt(name, default):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default


tag, cap, frames = sys.argv[1], sys.argv[2], int(sys.argv[3])
every, first = int(opt("--every", "50")), int(opt("--from", "600"))
# variants: --env "K=V;K=V" for B and C (e.g. F2b's lamp knobs GT4_N3_GLOW_RANGE / _INTENSITY), --benv for B only
# (e.g. GT4_N3_V4NARROW); --host another debug host; A (the GS path) is cached per capture and frame set in
# build/sessions/n3dark_A_<capture>_<first>_<every>/ and reused
kv = lambda s: dict(x.split("=", 1) for x in s.split(";") if x)
both, bonly = kv(opt("--env", "")), kv(opt("--benv", ""))
hostexe = Path(opt("--host", str(CLONE / "build/lanef_run/gt4_host_n3orient.exe")))
out = CLONE / "build" / "sessions" / f"n3dark_{tag}"
shutil.rmtree(out, ignore_errors=True)
out.mkdir(parents=True)
acache = CLONE / "build" / "sessions" / f"n3dark_A_{Path(cap).stem}_{first}_{frames}_{every}"
runs = {"A": ("original", {}), "B": ("enhanced", {"GT4_N3_LOG": "1", "GT4_N3_V4SKIP": "1", **both, **bonly}),
        "C": ("enhanced", dict(both))}
shots, logs = {}, {}
for key, (night, env) in runs.items():
    d = acache if key == "A" else out / key
    if key == "A" and (acache / "done").exists():
        shots[key] = dict(fm.frame_files(str(d)))
        logs[key] = ""
        log(f"A: cached ({acache.name})")
        continue
    shutil.rmtree(d, ignore_errors=True)
    d.mkdir()
    locks = take_slot(f"N3 never-dark {tag}: {key} ({night})")
    try:
        r = subprocess.run([str(hostexe), "--headless", "--size", "640x360", "--replay", cap,
                            "--frames", str(frames), "--fixed-step", "--preset", "original", "--set", "display.frame_interpolation=off",
                            "--set", "texture.hd_pack=off", "--set", f"lighting.night={night}", "--capture-frames",
                            f"{first}-{frames}/{every}", "--capture-dir", str(d)], capture_output=True, text=True,
                           creationflags=subprocess.CREATE_NO_WINDOW, env=dict(os.environ, GT4_HOST_RECORD_SCENE="1", **env))
        logs[key] = r.stdout + r.stderr
        log(f"{key}: exit {r.returncode}")
    finally:
        for lock in locks:
            shutil.rmtree(lock, ignore_errors=True)
    shots[key] = dict(fm.frame_files(str(d)))
    if key == "A" and r.returncode == 0:
        (acache / "done").write_text("ok\n")
log(f"variant: B and C {both or 'defaults'}; B only {bonly or '-'}; host {hostexe.name}")
y = lambda im: 0.2126 * im[..., 0] + 0.7152 * im[..., 1] + 0.0722 * im[..., 2]
rows = []
for n in sorted(shots["A"]):
    if n not in shots["B"] or n not in shots["C"]:
        continue
    a, b, c = (fm.read_ppm(shots[k][n]).astype(np.float32) for k in "ABC")
    patch = np.abs(b - c).max(axis=2) > 2
    if patch.sum() < 50:
        rows.append((n, 0, 0.0, 0.0, 0.0))
        continue
    d = (y(b) - y(a))[patch]
    rows.append((n, int(patch.sum()), float(d.mean()), float((d < -4).mean()), float((d > 4).mean())))
    if (d < -4).mean() > 0.05 or (d > 4).mean() > 0.5:
        img = (a * 0.5).astype(np.uint8)
        dd = y(b) - y(a)
        img[patch & (dd < -4)] = [255, 40, 40]
        img[patch & (dd > 4)] = [40, 220, 40]
        Image.fromarray(np.concatenate([a.astype(np.uint8), b.astype(np.uint8), img], axis=1)).save(out / f"dark_{n:05d}.png")
with_patch = [r for r in rows if r[1]]
print(f"{tag}: {len(rows)} frames, {len(with_patch)} with skipped patches")
if with_patch:
    print(f"  in the patches, skip + F2b against the GS path: mean change {np.mean([r[2] for r in with_patch]):+.1f} levels; "
          f"frames with more than 5 % of the patch darker by over 4 levels: {sum(1 for r in with_patch if r[3] > 0.05)}")
    tot = sum(r[1] for r in with_patch)
    print(f"  over all frames: patch pixels {tot}, darker by over 4 levels {sum(r[1] * r[3] for r in with_patch) / tot * 100:.1f} %; "
          f"patch per frame {tot / len(rows):.0f} px of {640 * 360}")
    for r in sorted(with_patch, key=lambda r: -r[3])[:6]:
        print(f"    frame {r[0]}: patch {r[1]} px, mean {r[2]:+.1f}, darker {r[3] * 100:.1f} %, brighter {r[4] * 100:.1f} %")
    print(f"  brighter by over 4 levels: {sum(r[1] * r[4] for r in with_patch) / tot * 100:.1f} % of patch pixels; "
          f"frames over 50 %: {sum(1 for r in with_patch if r[4] > 0.5)}; the most:")
    for r in sorted(with_patch, key=lambda r: -r[4])[:4]:
        print(f"    frame {r[0]}: patch {r[1]} px, mean {r[2]:+.1f}, darker {r[3] * 100:.1f} %, brighter {r[4] * 100:.1f} %")
for l in logs["B"].splitlines():
    if l.startswith("F2b course lamps"):
        print("  " + l[:220])
print(f"  v4 skips: {sum(1 for l in logs['B'].splitlines() if l.startswith('N3 v4:') and l.endswith('skip'))} items judged skip "
      f"of {sum(1 for l in logs['B'].splitlines() if l.startswith('N3 v4:'))}")
(out / "B.log").write_text("\n".join(l for l in logs["B"].splitlines() if l.startswith(("N3 v4:", "F2b"))), encoding="utf-8")
for k in "BC":
    shutil.rmtree(out / k, ignore_errors=True)
