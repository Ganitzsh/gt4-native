#!/usr/bin/env python3
"""n4_orig.py CAP FRAMES SPAN: N4's ORIG check. The capture replayed by the host before N4 (build/lanef_run/
gt4_host_f2perf.exe, 5deeb00) and the build folder's host (copied to gt4_host_n4.exe), frames SPAN (A-B) at 640x360,
with the original preset, then with lighting.night = enhanced (12 test lamps). Frames compared byte for byte per
setting. One heavy slot per replay."""
import os
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, "C:/Users/julie/gt4-lane-F/tools")
from r5_session import take_slot, log, CLONE  # noqa: E402
import frame_metrics as fm  # noqa: E402

cap, frames, span = sys.argv[1], int(sys.argv[2]), sys.argv[3]
old = CLONE / "build" / "lanef_run" / "gt4_host_f2perf.exe"
new = CLONE / "build" / "lanef_run" / "gt4_host_n4.exe"
shutil.copy2(CLONE / "build/host-windows-Release/gt4_host.exe", new)
outs = {}
for night in ("original", "enhanced"):
    for name, host in (("old", old), ("new", new)):
        out = CLONE / "build" / "sessions" / f"n4_{night}_{name}"
        shutil.rmtree(out, ignore_errors=True)
        out.mkdir(parents=True)
        extra = {"GT4_NIGHT_TEST_LIGHTS": "12"} if night == "enhanced" else {}
        locks = take_slot(f"N4 ORIG replay ({night}, {name} host)")
        try:
            r = subprocess.run([str(host), "--headless", "--size", "640x360", "--replay", str(CLONE / "build/caps" / cap),
                                "--frames", str(frames), "--fixed-step", "--preset", "original", "--set",
                                "display.frame_interpolation=off", "--set", "texture.hd_pack=off", "--set",
                                f"lighting.night={night}", "--capture-frames", span, "--capture-dir", str(out)],
                               capture_output=True, text=True, env=dict(os.environ, **extra),
                               creationflags=subprocess.CREATE_NO_WINDOW)
            (out / "host.log").write_text(r.stdout + r.stderr, encoding="utf-8", errors="replace")
            log(f"replay {night} {name}: exit {r.returncode}")
        finally:
            for lock in locks:
                shutil.rmtree(lock, ignore_errors=True)
        outs[(night, name)] = out
for night in ("original", "enhanced"):
    a = dict(fm.frame_files(str(outs[(night, "old")])))
    b = dict(fm.frame_files(str(outs[(night, "new")])))
    both = sorted(set(a) & set(b))
    differ = [n for n in both if Path(a[n]).read_bytes() != Path(b[n]).read_bytes()]
    print(f"{night}: frames old {len(a)}, new {len(b)}, compared {len(both)}, differ {len(differ)} {differ[:10]}")
    for name in ("old", "new"):
        log_text = (outs[(night, name)] / "host.log").read_text(encoding="utf-8", errors="replace")
        n4 = [l for l in log_text.splitlines() if "N4" in l]
        print(f"  {name} host log lines with N4: {n4[:3]}")
