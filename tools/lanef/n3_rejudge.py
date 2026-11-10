#!/usr/bin/env python3
"""n3_rejudge.py HOST: why N3 step 1's map judged 248,441 items on lap_ssr5f_boot (29d4680's gates, 2026-11-10).
lap_ssr5f_boot to 10,000 with the verdicts on, GT4_N3_LOG and GT4_SCENE_CACHE_LOG: the judged count per scene frame,
and how often one (texture, material, item) is judged again, with the same measures or not. One heavy slot."""
import collections
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from r5_session import take_slot, CLONE  # noqa: E402

host = Path(sys.argv[1])
OUT = CLONE / "build/sessions/n3_rejudge"
shutil.rmtree(OUT, ignore_errors=True)
OUT.mkdir(parents=True)
locks = take_slot("lane F: N3 map re-judging check (lap_ssr5f_boot to 10,000)")
try:
    r = subprocess.run([str(host), "--headless", "--size", "640x360", "--replay", str(CLONE / "build/caps/lap_ssr5f_boot.cap"),
                        "--frames", "10000", "--fixed-step", "--preset", "original", "--set", "display.frame_interpolation=off",
                        "--set", "texture.hd_pack=off", "--set", "lighting.night=enhanced"],
                       capture_output=True, text=True, creationflags=subprocess.CREATE_NO_WINDOW,
                       env=dict(os.environ, GT4_HOST_RECORD_SCENE="1", GT4_GSN_SCENE_SPLIT="1", GT4_SCENE_CACHE_LOG="1",
                                GT4_N3_VERDICTS="1", GT4_N3_LOG="1"))
finally:
    for lock in locks:
        shutil.rmtree(lock, ignore_errors=True)
text = r.stdout + r.stderr
per = [(int(v), int(j), int(mn)) for v, mn, j in
       re.findall(r"scene cache: vblank (\d+): .*?meshes new (\d+), .*?n3 judged (\d+),", text)]
print(f"scene frames {len(per)}; judged total {sum(j for _, j, _ in per)}; frames judging any {sum(1 for _, j, _ in per if j)}")
late = [(v, j, mn) for v, j, mn in per if v >= 8500]
print(f"from VBlank 8,500 (racing): {len(late)} frames, judged {sum(j for _, j, _ in late)}, "
      f"meshes new {sum(mn for _, _, mn in late)}")
for v, j, mn in late[:: max(1, len(late) // 12)]:
    print(f"  vblank {v}: judged {j}, meshes new {mn}")
lines = re.findall(r"n3 verdict: texture (\w+) material (\d+) item (\d+) (.*?) -> (\w+)", text)
by = collections.Counter((t, m, i) for t, m, i, _, _ in lines)
same = collections.defaultdict(set)
for t, m, i, meas, v in lines:
    same[(t, m, i)].add(meas)
print(f"verdict lines {len(lines)}; distinct (texture, material, item) {len(by)}; judged more than once {sum(1 for c in by.values() if c > 1)}")
for k, c in by.most_common(5):
    print(f"  {k}: {c} times, {len(same[k])} distinct measures")
(OUT / "host.log").write_text("\n".join(l for l in text.splitlines() if not l.startswith("scene cache: vblank"))[-300000:],
                              encoding="utf-8", errors="replace")
