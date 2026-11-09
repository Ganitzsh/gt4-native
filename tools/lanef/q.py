#!/usr/bin/env python3
"""q.py SESSION_NAME CMD [CMD ...]: queue session commands at once. A CMD 'shot:NAME' captures NAME, waits for
it and writes NAME.png (and NAME_row.png, the bottom 4:3 strip) beside it. 'tap:BUTTON' is a one-host-frame tap."""
import sys, time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from r5_session import read_ppm, CLONE
d = CLONE / "build" / "sessions" / sys.argv[1]
seq = 0
def queue(*cmds):
    global seq
    base = time.time_ns()
    for c in cmds:
        name = f"{base + seq:020d}-{seq:08x}"; seq += 1
        t = d / "commands" / (name + ".tmp"); t.write_text(c + "\n"); t.rename(d / "commands" / (name + ".cmd"))
for c in sys.argv[2:]:
    if c.startswith("shot:"):
        n = c[5:]; queue(f"capture {n}")
        p = d / "frames" / (n + ".ppm")
        while not p.exists() or p.stat().st_size == 0: time.sleep(0.5)
        time.sleep(0.5)
        im = read_ppm(p); im.save(p.with_suffix(".png"))
        w, h = im.size; im.crop((w // 8, int(h * 0.70), w - w // 8, h)).save(d / "frames" / (n + "_row.png"))
        print(p.with_suffix(".png"))
    elif c.startswith("tap:"):
        b = c[4:]; queue(f"button {b} 1", "wait 1", f"button {b} 0", "vwait 60")
    else:
        queue(c)
