#!/usr/bin/env python3
"""n3_verdict_check.py: N3 step 1's verdict map against the debug hunt (2026-11-09). Replays CAP to FRAMES on the clone's
own host (build/host-windows-Release/gt4_host.exe, copied) with the scene split, GT4_N3_VERDICTS=1 and GT4_N3_LOG=1,
night enhanced; then the texture identities its 'n3 verdict' lines skip against the debug hunt's v4.log skips (the
'identity' of each 'N3 v4: ... -> skip' line), and the counters from the summary. One heavy slot.
    py n3_verdict_check.py CAP FRAMES HUNT_TAG [DAY_CAP DAY_FRAMES]
With a day capture: the same replay of it must judge nothing (no kFrameLampsOn)."""
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from r5_session import take_slot, log, CLONE  # noqa: E402

cap, frames, tag = sys.argv[1], int(sys.argv[2]), sys.argv[3]
host = CLONE / "build/lanef_run/gt4_host_n3s1.exe"
shutil.copy2(CLONE / "build/host-windows-Release/gt4_host.exe", host)
out = CLONE / "build/sessions/n3_verdict_check"
shutil.rmtree(out, ignore_errors=True)
out.mkdir(parents=True)


def replay(c, n, name):
    env = dict(os.environ, GT4_HOST_RECORD_SCENE="1", GT4_GSN_SCENE_SPLIT="1", GT4_N3_VERDICTS="1", GT4_N3_LOG="1")
    locks = take_slot(f"lane F: N3 step 1 verdict check ({name})")
    try:
        r = subprocess.run([str(host), "--headless", "--size", "640x360", "--replay", c, "--frames", str(n), "--fixed-step",
                            "--preset", "original", "--set", "display.frame_interpolation=off", "--set", "texture.hd_pack=off",
                            "--set", "lighting.night=enhanced"], capture_output=True, text=True, env=env,
                           creationflags=subprocess.CREATE_NO_WINDOW)
    finally:
        for lock in locks:
            shutil.rmtree(lock, ignore_errors=True)
    text = r.stdout + r.stderr
    (out / f"{name}.log").write_text("\n".join(l for l in text.splitlines() if l.startswith("n3 ") or "n3 items" in l),
                                     encoding="utf-8")
    log(f"{name}: exit {r.returncode}")
    return text


text = replay(cap, frames, "night")
mine = {}
for m in re.finditer(r"n3 verdict: texture (\w+) material (\d+) item (\d+) .* -> (\w+)", text):
    mine.setdefault(m.group(1), set()).add(m.group(4))
skip_mine = {t for t, v in mine.items() if "skip" in v}
hunt = (CLONE / "build/sessions" / f"n3v4b_{tag}" / "v4.log").read_text(encoding="utf-8").splitlines()
skip_hunt = {re.search(r"identity (\w+)", l).group(1) for l in hunt if l.endswith("skip")}
print(f"verdict lines: {sum(1 for _ in re.finditer('n3 verdict:', text))}; identities judged {len(mine)}, skipped {len(skip_mine)}")
print(f"debug hunt skipped identities: {len(skip_hunt)}; both {len(skip_mine & skip_hunt)}; only the map "
      f"{sorted(skip_mine - skip_hunt)[:10]}; only the hunt {sorted(skip_hunt - skip_mine)[:10]}")
for l in text.splitlines():
    if "n3 items judged" in l:
        print(l[l.index("n3 items judged"):][:200])
if len(sys.argv) > 5:
    day = replay(sys.argv[4], int(sys.argv[5]), "day")
    print(f"day: verdict lines {sum(1 for _ in re.finditer('n3 verdict:', day))} (expected 0)")
    for l in day.splitlines():
        if "n3 items judged" in l:
            print("day: " + l[l.index("n3 items judged"):][:200])
