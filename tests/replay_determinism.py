#!/usr/bin/env python3
"""A replay comes out the same every run with frame interpolation and the HD texture pack on (PLAN.md C1).

    replay_determinism.py GT4_HOST [FRAMES]

Replays the capture named by GT4_DETERMINISM_CAPTURE twice, headless (--fixed-step: the interpolation phase from the
presents, texture pack replacements decoded at first use), with display.frame_interpolation=on and
texture.hd_pack=on at --preset ultra, and requires every frame to match bit for bit. A capture with 3D (a race:
race7.cap) is what tests it; the GS test's synthetic capture has no 3D batches to interpolate. Captures are not in
the repository (docs/modern/CAPTURES.md), so without GT4_DETERMINISM_CAPTURE the test is skipped (exit code 77).
Without a texture pack on the machine the HD part has nothing to load and only interpolation is tested.
"""
import os
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "tools"))
from frame_diff import read_ppm  # noqa: E402

SKIP = 77


def replay(host, capture, frames, out):
    args = [host, "--headless", "--size", "640x360", "--replay", capture, "--frames", str(frames), "--preset", "ultra",
            "--set", "display.frame_interpolation=on", "--set", "texture.hd_pack=on",
            "--capture-frames", f"1-{frames}", "--capture-dir", out]
    kw = {"creationflags": subprocess.CREATE_NO_WINDOW} if os.name == "nt" else {}
    r = subprocess.run(args, capture_output=True, text=True, **kw)
    if r.returncode != 0:
        print(r.stdout[-2000:], r.stderr[-2000:])
        sys.exit(f"gt4_host exited {r.returncode}")
    return r.stdout + r.stderr


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    host, frames = sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 100
    capture = os.environ.get("GT4_DETERMINISM_CAPTURE", "")
    if not capture or not os.path.isfile(capture):
        print("skipped: set GT4_DETERMINISM_CAPTURE to a capture with 3D (race7.cap, docs/modern/CAPTURES.md)")
        return SKIP
    work = tempfile.mkdtemp(prefix=f"gt4_determinism_{os.getpid()}_")
    try:
        dirs = [os.path.join(work, f"run{i}") for i in (0, 1)]
        logs = []
        for d in dirs:
            os.makedirs(d)
            logs.append(replay(host, capture, frames, d))
        names = sorted(n for n in os.listdir(dirs[0]) if n.endswith(".ppm"))
        if len(names) < frames or sorted(os.listdir(dirs[1])) != sorted(os.listdir(dirs[0])):
            sys.exit(f"the runs wrote {len(names)} and {len(os.listdir(dirs[1]))} frames, {frames} asked")
        differ = [n for n in names if read_ppm(os.path.join(dirs[0], n)) != read_ppm(os.path.join(dirs[1], n))]
        pack = next((line.strip() for line in logs[0].splitlines() if line.startswith("Texture pack:")), "no texture pack")
        print(f"{len(names) - len(differ)} of {len(names)} frames identical between two replays ({pack})")
        if differ:
            print("differing:", ", ".join(n[6:12] for n in differ[:20]))
            return 1
        return 0
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
