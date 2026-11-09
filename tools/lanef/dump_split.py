#!/usr/bin/env python3
"""dump_split.py VBLANK[:K] ...: for each VBLANK (optionally with GT4_GSN_LIMIT=K), replay r5_ss_ab.cap up to it and
dump every colour target of that frame (GT4_GSN_DUMP) into build/sessions/split/<vblank>_<K>/. One heavy slot."""
import os
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from r5_session import take_slot, log, CLONE  # noqa: E402

CAP = CLONE / "build" / "caps" / "r5_ss_ab.cap"
FIRST = 13266                                     # the capture's first frame's VBlank
host = CLONE / "build" / "lanef_run" / "gt4_host_split.exe"
host.parent.mkdir(parents=True, exist_ok=True)
shutil.copy2(CLONE / "build/host-windows-Release/gt4_host.exe", host)
locks = take_slot("R10 split: target dumps of r5_ss_ab.cap")
try:
    for spec in sys.argv[1:]:
        vb, _, k = spec.partition(":")
        out = CLONE / "build" / "sessions" / "split" / f"{vb}_{k or 'all'}"
        shutil.rmtree(out, ignore_errors=True)
        out.mkdir(parents=True)
        env = dict(os.environ, GT4_GSN_DUMP=vb)
        if k:
            env["GT4_GSN_LIMIT"] = k
        frames = int(vb) - FIRST + 4
        args = [str(host), "--headless", "--size", "640x448", "--replay", str(CAP), "--frames", str(frames),
                "--fixed-step", "--preset", "original", "--set", "display.frame_interpolation=off",
                "--set", "texture.hd_pack=off", "--capture", str(out / "display.ppm")]
        r = subprocess.run(args, cwd=out, env=env, capture_output=True, text=True,
                           creationflags=subprocess.CREATE_NO_WINDOW)
        dumped = sorted(p.name for p in out.glob("gsn_*.ppm"))
        log(f"{spec}: exit {r.returncode}, {len(dumped)} targets dumped: {dumped[:12]}")
finally:
    for lock in locks:
        shutil.rmtree(lock, ignore_errors=True)
    log("slot released")
