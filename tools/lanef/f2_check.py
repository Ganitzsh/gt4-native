#!/usr/bin/env python3
"""f2_check.py CAP FRAMES [TESTLIGHTS]: F2 step 2b on a capture with scene records. Two replays with the build folder's
host, every frame at 640x360, profiles on: 'off' (lighting.night original) and 'on' (lighting.night = enhanced,
GT4_NIGHT_TEST_LIGHTS=TESTLIGHTS, Vulkan validation with sync validation). Then: the validation messages, the frames
that differ and by how much, and the scene stage's lighting GPU time from each profile. One heavy slot per replay."""
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, "C:/Users/julie/gt4-lane-F/tools")
from r5_session import take_slot, log, CLONE  # noqa: E402
import frame_metrics as fm  # noqa: E402

cap, frames = sys.argv[1], int(sys.argv[2])
tests = sys.argv[3] if len(sys.argv) > 3 else "12"
span = sys.argv[4] if len(sys.argv) > 4 else ""   # A-B: only those frames captured (--capture-frames)
host = CLONE / "build" / "lanef_run" / "gt4_host_f2.exe"
shutil.copy2(CLONE / "build/host-windows-Release/gt4_host.exe", host)
outs = {}
for name, night, extra in (("off", "original", {}),
                           ("on", "enhanced", {"GT4_NIGHT_TEST_LIGHTS": tests, "VK_INSTANCE_LAYERS": "VK_LAYER_KHRONOS_validation",
                                               "VK_LAYER_VALIDATE_SYNC": "1"})):
    out = CLONE / "build" / "sessions" / f"f2_{name}"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    locks = take_slot(f"F2 check replay ({name})")
    try:
        r = subprocess.run([str(host), "--headless", "--size", "640x360", "--replay", str(CLONE / "build/caps" / cap),
                            "--frames", str(frames), "--fixed-step", "--preset", "original", "--set",
                            "display.frame_interpolation=off", "--set", "texture.hd_pack=off", "--set", f"lighting.night={night}",
                            *(["--capture-frames", span, "--capture-dir", str(out)] if span else
                              ["--capture", str(out / "f.ppm"), "--capture-every", "1"]),
                            "--profile-json", str(out / "profile.json")],
                           capture_output=True, text=True, env=dict(os.environ, **extra), creationflags=subprocess.CREATE_NO_WINDOW)
        (out / "host.log").write_text(r.stdout + r.stderr, encoding="utf-8", errors="replace")
        log(f"replay {name}: exit {r.returncode}")
    finally:
        for lock in locks:
            shutil.rmtree(lock, ignore_errors=True)
    outs[name] = out
text = (outs["on"] / "host.log").read_text(encoding="utf-8", errors="replace")
vuids = [l for l in text.splitlines() if "VUID" in l or "Validation Error" in l or "SYNC-HAZARD" in l]
print(f"validation messages (on): {len(vuids)}")
for l in vuids[:8]:
    print("   ", l[:300])
a, b = dict(fm.frame_files(str(outs["off"]))), dict(fm.frame_files(str(outs["on"])))
changed = []
for n in sorted(set(a) & set(b)):
    x, y = fm.read_ppm(a[n]).astype(int), fm.read_ppm(b[n]).astype(int)
    d = np.abs(y - x)
    if d.any():
        changed.append((n, float(d.mean()), float((y - x).mean()), float((d.max(axis=2) > 0).mean())))
print(f"frames compared {len(set(a) & set(b))}, changed {len(changed)}")
for n, m, s, frac in changed[:: max(1, len(changed) // 12)]:
    print(f"  frame {n}: mean |diff| {m:.2f}, mean diff {s:+.2f}, pixels changed {100 * frac:.1f} %")
for name in ("off", "on"):
    p = outs[name] / "profile.json"
    try:
        prof = json.loads(p.read_text())
        keys = [k for k in json.dumps(prof).split('"') if "light" in k.lower()]
        print(f"profile {name}: keys with 'light': {sorted(set(keys))[:10]}")
    except Exception as e:  # noqa: BLE001
        print(f"profile {name}: {e}")
