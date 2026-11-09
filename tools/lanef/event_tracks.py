#!/usr/bin/env python3
"""event_tracks.py SESSION FIRST LAST: from a hall's event list (pointer on the first event), open events FIRST..LAST
(0-based, top to bottom), accept the regulations, and save the track row at the start and after four steps right
as ev<N>.png (both strips stacked), then back out to the list.

Guarded: the hall list's title strip is taken as a reference first, and before each event a fresh screenshot must
match it, or the script stops (no blind commands into an unknown screen). Every capture name is new."""
import sys
import time
from pathlib import Path

from PIL import Image, ImageChops, ImageStat

sys.path.insert(0, str(Path(__file__).resolve().parent))
from r5_session import read_ppm, CLONE  # noqa: E402

d = CLONE / "build" / "sessions" / sys.argv[1]
first, last = int(sys.argv[2]), int(sys.argv[3])
run = time.strftime("%H%M%S")
seq = [0]
shots = [0]


def queue(*cmds):
    base = time.time_ns()
    for c in cmds:
        name = f"{base + seq[0]:020d}-{seq[0]:08x}"
        seq[0] += 1
        t = d / "commands" / (name + ".tmp")
        t.write_text(c + "\n")
        t.rename(d / "commands" / (name + ".cmd"))


def press(*steps):
    cmds = []
    for b, w in zip(steps[::2], steps[1::2]):
        cmds += [f"button {b} 1", "vwait 6", f"button {b} 0", f"vwait {w}"]
    queue(*cmds)


def shot(tag, timeout=300):
    shots[0] += 1
    name = f"et_{run}_{shots[0]:03d}_{tag}"
    queue(f"capture {name}")
    p = d / "frames" / (name + ".ppm")
    t0 = time.time()
    while not p.exists() or p.stat().st_size == 0:
        if time.time() - t0 > timeout:
            raise SystemExit(f"no capture {name}")
        time.sleep(0.5)
    time.sleep(0.5)
    return read_ppm(p)


def title(im):
    return im.resize((1280, 720)).crop((180, 15, 700, 75)).convert("L")


ref = title(shot("list"))
ref.save(d / "frames" / f"et_{run}_ref.png")
for n in range(first, last + 1):
    now = title(shot(f"check{n}"))
    diff = ImageStat.Stat(ImageChops.difference(ref, now)).mean[0]
    if diff > 12:
        raise SystemExit(f"not on the hall list before event {n} (title differs by {diff:.1f}): stopping")
    press(*(["dpdown", 120] * n))                      # from the top of the list to event n (the list scrolls)
    press("cross", 500, "cross", 600)                  # open; the regulations dialog opens on OK: accept
    a = shot(f"ev{n}a").crop((160, 480, 1120, 720))
    press(*(["dpright", 30] * 4))
    b = shot(f"ev{n}b").crop((160, 480, 1120, 720))
    out = Image.new("RGB", (960, 480))
    out.paste(a, (0, 0))
    out.paste(b, (0, 240))
    out.resize((640, 320)).save(d / "frames" / f"ev{n}.png")
    press("triangle", 500)                             # back to the list
    press(*(["dpup", 120] * n))
    print(f"ev{n}.png", flush=True)
