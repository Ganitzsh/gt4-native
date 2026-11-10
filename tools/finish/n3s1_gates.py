#!/usr/bin/env python3
"""N3 step 1's verdict map, its gates, from lane F's tools/lanef/n3s1_gates.py with today's paths.

    py -I n3s1_gates.py STEP [STEP ...]      (under tools/slot.py, a heavy slot)

STEP: orig | verdicts_on | verdicts_off | native_race7 | native_ssr5f
HOST is the worktree's clean build (build_windows.cmd Release, ctest included); REF is main's host kept aside.
"""
import hashlib
import os
import re
import shutil
import statistics
import subprocess
import sys
import time
from pathlib import Path

WT = Path(r"C:\Users\julie\gt4-wt-land")
CAPS = Path(r"C:\Users\julie\gt4-native-game\build\caps")
HOST = WT / "build/host-windows-Release/gt4_host.exe"
REF = WT / "build/ref/gt4_host_main.exe"
OUT = WT / "build/n3s1"
NW = subprocess.CREATE_NO_WINDOW


def replay(name, cap, frames, args, env=None, exe=HOST):
    start = time.time()
    r = subprocess.run([str(exe), "--headless", "--replay", str(cap), "--frames", str(frames), "--fixed-step", *args],
                       capture_output=True, text=True, errors="replace", creationflags=NW,
                       env=dict(os.environ, **(env or {})))
    text = r.stdout + r.stderr
    (OUT / f"{name}.log").write_text(
        "\n".join(l for l in text.splitlines() if not l.startswith("scene cache: vblank"))[-200000:],
        encoding="utf-8", errors="replace")
    print(f"   ({name}: exit {r.returncode}, {time.time() - start:.0f} s)", flush=True)
    return text


def orig():
    ok_all = True
    for cap, frames, every in (("race7", 100, 1), ("race_grid43", 700, 1), ("race_boot", 4914, 10)):
        d = OUT / f"orig_{cap}"
        shutil.rmtree(d, ignore_errors=True)
        d.mkdir(parents=True)
        replay(f"orig_{cap}", CAPS / f"{cap}.cap", frames,
               ["--size", "1280x960", "--preset", "original", "--set", "texture.hd_pack=off",
                "--set", "display.frame_interpolation=off", "--set", "debug.overlay=off",
                "--capture", str(d / "f.ppm"), "--capture-every", str(every)])
        want = {}
        for line in (CAPS / f"orig_r6_{cap}.sha256").read_text(encoding="utf-8").splitlines():
            if line and not line.startswith("#"):
                n, h = line.split()
                want[n] = h.upper()
        got = {p.name: hashlib.sha256(p.read_bytes()).hexdigest().upper() for p in d.glob("f*.ppm")}
        missing = [n for n in want if n not in got]
        diff = [n for n in want if n in got and got[n] != want[n]]
        ok = not missing and not diff
        ok_all &= ok
        print(f"ORIG {cap}: {len(want)} listed, {len(want) - len(missing) - len(diff)} identical, {len(diff)} differ, "
              f"{len(missing)} missing: {'pass' if ok else 'FAIL'}", flush=True)
        shutil.rmtree(d, ignore_errors=True)
    return ok_all


def verdicts(name):
    common = ["--size", "640x360", "--preset", "original", "--set", "display.frame_interpolation=off", "--set",
              "texture.hd_pack=off", "--set", "lighting.night=enhanced"]
    extra = {"GT4_N3_VERDICTS": "1", "GT4_N3_LOG": "1"} if name == "on" else {}
    text = replay(f"ssr5f_{name}", CAPS / "lap_ssr5f_boot.cap", 20200, common,
                  dict(GT4_HOST_RECORD_SCENE="1", GT4_GSN_SCENE_SPLIT="1", GT4_SCENE_CACHE_LOG="1", **extra))
    vals = [(int(v), float(c)) for v, c in re.findall(r"scene cache: vblank (\d+): .*?\(cpu ([\d.]+) us\)", text)]
    xs = sorted(c for v, c in vals if v >= 8000)
    if name == "on":
        skips = sorted({m.group(1) for m in re.finditer(r"n3 verdict: texture (\w+) .* -> skip", text)})
        print(f"verdicts on (lap_ssr5f_boot to 20,200): the map skips {len(skips)} identities, their list's sha "
              f"{hashlib.sha256(','.join(skips).encode()).hexdigest()[:16]}", flush=True)
        lines = re.findall(r"n3 verdict: texture (\w+) material (\d+) item (\d+) (.*)", text)
        from collections import Counter
        keys = Counter((t, i, rest) for t, m, i, rest in lines)
        print(f"   verdict lines {len(lines)}; distinct (texture, item, measures) {len(keys)}; the most repeated: "
              f"{keys.most_common(3)}", flush=True)
        vb = [int(v) for v in re.findall(r"scene cache: vblank (\d+):", text)]
        print(f"   scene frames logged {len(vb)}, vblanks {min(vb)}-{max(vb)}", flush=True)
    s = re.search(r"n3 items judged.*?views misaligned \d+", text)
    print("   " + (s.group(0) if s else "no n3 summary"), flush=True)
    if xs:
        print(f"   B6 thread CPU per scene frame, race only, verdicts {name}: {len(xs)} frames, mean "
              f"{statistics.mean(xs):.1f} us, p95 {xs[int(0.95 * (len(xs) - 1))]:.1f} us, max {xs[-1]:.1f} us", flush=True)
    return bool(xs)


def native(cap, frames, shots):
    args = ["--size", "1280x896", "--preset", "original", "--set", "render.gs=native", "--set", "render.quality=native",
            "--set", "aspect.ratio=window", "--set", "texture.hd_pack=off", "--set", "display.frame_interpolation=off",
            "--set", "debug.overlay=off"]
    hashes = {}
    for who, exe in (("main", REF), ("new", HOST)):
        d = OUT / f"native_{cap.stem}_{who}"
        shutil.rmtree(d, ignore_errors=True)
        d.mkdir(parents=True)
        grab = (["--capture-frames", shots, "--capture-dir", str(d)] if shots
                else ["--capture", str(d / "f.ppm"), "--capture-every", "1"])
        replay(f"native_{cap.stem}_{who}", cap, frames, args + grab, exe=exe)
        hashes[who] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in d.glob("*.ppm")}
        first = next(iter(sorted(d.glob("*.ppm"))), None)
        if first:
            print(f"   {who}: {first.read_bytes()[:20].split()[1:3]}", flush=True)
        shutil.rmtree(d, ignore_errors=True)
    a, b = hashes["main"], hashes["new"]
    same = sum(1 for n in a if b.get(n) == a[n])
    ok = bool(a) and same == len(a) == len(b)
    print(f"NATIVE {cap.stem}: main {len(a)} frames, new {len(b)}; identical {same}, differ "
          f"{sum(1 for n in a if n in b and b[n] != a[n])}: {'pass' if ok else 'FAIL'}", flush=True)
    return ok


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    sys.path.insert(0, r"C:\Users\julie\gt4-native-game\tools")
    try:
        import slot
        slot.kill_with_me()
    except Exception as e:
        print(f"(no kill_with_me: {e})")
    ok = True
    for step in sys.argv[1:]:
        if step == "orig":
            ok &= orig()
        elif step in ("verdicts_on", "verdicts_off"):
            ok &= verdicts(step.split("_")[1])
        elif step == "native_race7":
            ok &= native(CAPS / "race7.cap", 100, None)
        elif step == "native_ssr5f":
            ok &= native(CAPS / "lap_ssr5f_boot.cap", 14000, "10000-14000/500")
        else:
            raise SystemExit(f"unknown step {step}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
