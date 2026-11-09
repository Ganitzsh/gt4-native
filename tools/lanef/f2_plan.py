#!/usr/bin/env python3
"""f2_plan.py CAP FRAMES RANGE: lane B's checks of F2 on the render graph. Replays (build folder's host, 640x360, the
original preset, 12 test lamps): 'on' (lighting.night = enhanced) and 'wrong' (the same with GT4_WRONG_PLAN_TEST=1, every
scene-stage plan taken as wrong, so the stage runs on the member images). The frames in RANGE must be identical, and
'on' must report no scene stage run on the members. Also 'off' against 'onNoLamps' (lighting.night = enhanced without
test lamps, on a capture without scene records): identical."""
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, "C:/Users/julie/gt4-lane-F/tools")
from r5_session import take_slot, log, CLONE  # noqa: E402
import frame_metrics as fm  # noqa: E402

cap, frames, span = sys.argv[1], int(sys.argv[2]), sys.argv[3]
host = CLONE / "build" / "lanef_run" / "gt4_host_f2plan.exe"
shutil.copy2(CLONE / "build/host-windows-Release/gt4_host.exe", host)
runs = {"on": ("enhanced", {"GT4_NIGHT_TEST_LIGHTS": "12"}),
        "wrong": ("enhanced", {"GT4_NIGHT_TEST_LIGHTS": "12", "GT4_WRONG_PLAN_TEST": "1"}),
        "off": ("original", {}),
        "onNoLamps": ("enhanced", {})}
outs = {}
for name, (night, extra) in runs.items():
    out = CLONE / "build" / "sessions" / f"f2plan_{name}"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    env = {k: v for k, v in os.environ.items() if k not in ("GT4_NIGHT_TEST_LIGHTS", "GT4_WRONG_PLAN_TEST")}
    env.update(extra)
    locks = take_slot(f"F2 plan check ({name})")
    try:
        r = subprocess.run([str(host), "--headless", "--size", "640x360", "--replay", str(CLONE / "build/caps" / cap),
                            "--frames", str(frames), "--fixed-step", "--preset", "original", "--set",
                            "display.frame_interpolation=off", "--set", "texture.hd_pack=off", "--set", f"lighting.night={night}",
                            "--capture-frames", span, "--capture-dir", str(out)],
                           capture_output=True, text=True, env=env, creationflags=subprocess.CREATE_NO_WINDOW)
        (out / "host.log").write_text(r.stdout + r.stderr, encoding="utf-8", errors="replace")
        log(f"{name}: exit {r.returncode}")
    finally:
        for lock in locks:
            shutil.rmtree(lock, ignore_errors=True)
    outs[name] = out


def compare(a, b):
    fa, fb = dict(fm.frame_files(str(outs[a]))), dict(fm.frame_files(str(outs[b])))
    common = sorted(set(fa) & set(fb))
    differ = [n for n in common if (fm.read_ppm(fa[n]) != fm.read_ppm(fb[n])).any()]
    print(f"{a} vs {b}: {len(common)} frames compared, {len(differ)} differ {differ[:8]}")


for name in outs:
    text = (outs[name] / "host.log").read_text(encoding="utf-8", errors="replace")
    members = re.findall(r"[Ss]cene stages? run on the members:? *(\d+)", text)
    wrong = len(re.findall(r"run on the member images", text))
    print(f"{name}: 'scene stages run on the members' {members[-1:] or 'not printed'}; 'run on the member images' lines {wrong}")
compare("on", "wrong")
compare("off", "onNoLamps")
compare("off", "on")
