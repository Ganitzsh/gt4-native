#!/usr/bin/env python3
"""n3s1_gates.py [SRC]: N3 step 1's verdict map, its gates (2026-11-10). On SRC's HEAD (default this clone; tree clean):
  1. a host from an empty folder (build/host-n3s1-<head>, BUILD_TESTING on) and the full ctest;
  2. ORIG: race7, race_grid43, race_boot at original against lane B's orig_r6 hash lists (the map is off by default);
  3. lap_ssr5f_boot to 20,200 with the verdicts on (GT4_N3_VERDICTS=1, GT4_N3_LOG=1) and off, night enhanced, scene
     split, GT4_SCENE_CACHE_LOG=1: the skipped identities against the debug hunt's (n3v4b_ssr5fb/v4.log), and B6's
     thread CPU per scene frame over the race (from 8,000) on and off: mean, p95, max;
  4. NATIVE (PLAN section 2, e66ec7b; the architect, 2026-11-10: byte-identical frames to main's host at NATIVE's
     settings meet it): 1280x896, preset original, render.gs native, quality native; race7 1-100 every frame and
     lap_ssr5f_boot 10,000-14,000 every 500, the verdicts off (the default), SRC's host against main's, built from
     empty in C:/Users/julie/gt4-lane-F-main (MAIN_SRC).
One heavy slot per job (hosts from 70e7fdb on read a replay without the file cache)."""
import hashlib
import os
import re
import shutil
import statistics
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from r5_session import take_slot, log, CLONE  # noqa: E402

NW = subprocess.CREATE_NO_WINDOW
LANE_B = Path("C:/Users/julie/gt4-lane-B/build/caps")


NATIVE_ONLY = "--native-only" in sys.argv   # step 4 alone, on the host steps 1-3 built for this head
SRC = Path(next((a for a in sys.argv[1:] if not a.startswith("--")), CLONE))
MAIN_SRC = Path("C:/Users/julie/gt4-lane-F-main")


def git(*a, src=None):
    return subprocess.run(["git", "-C", str(src or SRC), *a], capture_output=True, text=True).stdout.strip()


if git("status", "--porcelain", "--untracked-files=no"):
    raise SystemExit("the tree isn't clean")
head = git("rev-parse", "--short=7", "HEAD")
OUT = CLONE / "build/sessions" / f"n3s1_gates_{head}"
if NATIVE_ONLY:
    OUT = OUT / "native_only"
shutil.rmtree(OUT, ignore_errors=True)
OUT.mkdir(parents=True)
bdir = f"build\\host-n3s1-{head}"
cmd = OUT / "build.cmd"
cmd.write_text(rf"""@echo off
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSPATH=%%i
call "%VSPATH%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -no_logo || exit /b 1
cd /d {SRC}
if exist {bdir} rmdir /s /q {bdir}
cmake -S . -B {bdir} -G Ninja -DCMAKE_BUILD_TYPE=Release -DGT4_BUILD_HOST=ON -DBUILD_TESTING=ON || exit /b 1
cmake --build {bdir} --parallel 6 || exit /b 1
ctest --test-dir {bdir} --output-on-failure || exit /b 1
""", encoding="ascii")
if not NATIVE_ONLY:
    locks = take_slot(f"lane F: N3 step 1 gates: host from an empty folder + ctest at {head}")
    try:
        b = subprocess.run(["cmd.exe", "/d", "/c", str(cmd)], capture_output=True, text=True, creationflags=NW)
    finally:
        for lock in locks:
            shutil.rmtree(lock, ignore_errors=True)
    (OUT / "build.log").write_text(b.stdout + b.stderr, encoding="utf-8", errors="replace")
    tests = re.search(r"(\d+)% tests passed, (\d+) tests failed out of (\d+)", b.stdout)
    print(f"1. build from empty + ctest: exit {b.returncode}; {tests.group(0) if tests else 'no ctest summary'}", flush=True)
    mine = [l for l in b.stdout.splitlines() if re.search(r"(lit_road|scene_cache|gs_frontend|scene_decoder|scene_frame)\.(cpp|h)\(\d+\): warning", l)
            and "C4996" not in l]
    print(f"   warnings in the touched files (C4996 aside): {len(mine)}", *mine[:5], sep="\n   ", flush=True)
    if b.returncode:
        raise SystemExit(1)
host = CLONE / "build/lanef_run" / f"gt4_host_n3s1_{head}.exe"
if not NATIVE_ONLY:
    shutil.copy2(SRC / bdir / "gt4_host.exe", host)


def replay(name, cap, frames, args, env=None, exe=None):
    locks = take_slot(f"lane F: N3 step 1 gates: {name}")
    try:
        r = subprocess.run([str(exe or host), "--headless", "--replay", str(cap), "--frames", str(frames), "--fixed-step", *args],
                           capture_output=True, text=True, creationflags=NW, env=dict(os.environ, **(env or {})))
    finally:
        for lock in locks:
            shutil.rmtree(lock, ignore_errors=True)
    text = r.stdout + r.stderr
    (OUT / f"{name}.log").write_text("\n".join(l for l in text.splitlines() if not l.startswith("scene cache: vblank"))[-200000:],
                                     encoding="utf-8", errors="replace")
    return text


if not NATIVE_ONLY:
    # 2. ORIG
    ok_all = True
    for cap, frames, every in (("race7", 100, 1), ("race_grid43", 700, 1), ("race_boot", 4914, 10)):
        d = OUT / f"orig_{cap}"
        d.mkdir()
        replay(f"orig_{cap}", LANE_B / f"{cap}.cap", frames,
               ["--size", "1280x960", "--preset", "original", "--set", "texture.hd_pack=off", "--set", "display.frame_interpolation=off",
                "--set", "debug.overlay=off", "--capture", str(d / "f.ppm"), "--capture-every", str(every)])
        want = {}
        for line in (LANE_B / f"orig_r6_{cap}.sha256").read_text(encoding="utf-8").splitlines():
            if line and not line.startswith("#"):
                n, h = line.split()
                want[n] = h.upper()
        got = {p.name: hashlib.sha256(p.read_bytes()).hexdigest().upper() for p in d.glob("f*.ppm")}
        missing = [n for n in want if n not in got]
        diff = [n for n in want if n in got and got[n] != want[n]]
        ok = not missing and not diff
        ok_all &= ok
        print(f"2. ORIG {cap}: {len(want)} listed, {len(want) - len(missing) - len(diff)} identical, {len(diff)} differ, "
              f"{len(missing)} missing: {'pass' if ok else 'FAIL'}", flush=True)
        shutil.rmtree(d, ignore_errors=True)
    print("   ORIG:", "pass" if ok_all else "FAIL", flush=True)

    # 3. the verdicts on the forward lap, and B6's cost on and off
    common = ["--size", "640x360", "--preset", "original", "--set", "display.frame_interpolation=off", "--set",
              "texture.hd_pack=off", "--set", "lighting.night=enhanced"]
    cpu = {}
    for name, extra in (("on", {"GT4_N3_VERDICTS": "1", "GT4_N3_LOG": "1"}), ("off", {})):
        text = replay(f"ssr5f_{name}", CLONE / "build/caps/lap_ssr5f_boot.cap", 20200, common,
                      dict(GT4_HOST_RECORD_SCENE="1", GT4_GSN_SCENE_SPLIT="1", GT4_SCENE_CACHE_LOG="1", **extra))
        vals = [(int(v), float(c)) for v, c in re.findall(r"scene cache: vblank (\d+): .*?\(cpu ([\d.]+) us\)", text)]
        cpu[name] = sorted(c for v, c in vals if v >= 8000)
        if name == "on":
            skip_mine = {m.group(1) for m in re.finditer(r"n3 verdict: texture (\w+) .* -> skip", text)}
            hunt = (CLONE / "build/sessions/n3v4b_ssr5fb/v4.log").read_text(encoding="utf-8").splitlines()
            skip_hunt = {re.search(r"identity (\w+)", l).group(1) for l in hunt if l.endswith("skip")}
            print(f"3. verdicts (lap_ssr5f_boot to 20,200): the map skips {len(skip_mine)} identities, the debug hunt "
                  f"{len(skip_hunt)}; both {len(skip_mine & skip_hunt)}; only the map {sorted(skip_mine - skip_hunt)}; only the "
                  f"hunt {sorted(skip_hunt - skip_mine)}", flush=True)
            s = re.search(r"n3 items judged.*?views misaligned \d+", text)
            print("   " + (s.group(0) if s else "no n3 summary"), flush=True)
    for name, xs in cpu.items():
        if xs:
            print(f"   B6 thread CPU per scene frame, race only, verdicts {name}: {len(xs)} frames, mean {statistics.mean(xs):.1f} us, "
                  f"p95 {xs[int(0.95 * (len(xs) - 1))]:.1f} us, max {xs[-1]:.1f} us", flush=True)

# 4. NATIVE: SRC's host against main's at NATIVE's settings, byte for byte, the verdicts off
mhead = git("rev-parse", "--short=7", "HEAD", src=MAIN_SRC)
mexe = CLONE / "build/lanef_run" / f"gt4_host_main_{mhead}.exe"
if not mexe.exists():
    mcmd = OUT / "build_main.cmd"
    mcmd.write_text(cmd.read_text(encoding="ascii").replace(f"cd /d {SRC}", f"cd /d {MAIN_SRC}")
                    .replace(bdir, f"build\\host-main-{mhead}").replace("ctest --test-dir", "rem ctest --test-dir"), encoding="ascii")
    locks = take_slot(f"lane F: N3 step 1 gates: main's host {mhead} from an empty folder (NATIVE's reference)")
    try:
        mb = subprocess.run(["cmd.exe", "/d", "/c", str(mcmd)], capture_output=True, text=True, creationflags=NW)
    finally:
        for lock in locks:
            shutil.rmtree(lock, ignore_errors=True)
    (OUT / "build_main.log").write_text(mb.stdout + mb.stderr, encoding="utf-8", errors="replace")
    print(f"4. main's host {mhead}: build exit {mb.returncode}", flush=True)
    if mb.returncode:
        raise SystemExit(1)
    shutil.copy2(MAIN_SRC / f"build/host-main-{mhead}/gt4_host.exe", mexe)
native = ["--size", "1280x896", "--preset", "original", "--set", "render.gs=native", "--set", "render.quality=native",
          "--set", "aspect.ratio=window",   # (f443d12: else pillarboxed to about 1195 x 896, not 2x)
          "--set", "texture.hd_pack=off", "--set", "display.frame_interpolation=off", "--set", "debug.overlay=off"]
for cap, frames, shots in ((LANE_B / "race7.cap", 100, None), (CLONE / "build/caps/lap_ssr5f_boot.cap", 14000, "10000-14000/500")):
    hashes = {}
    for who, exe in (("main", mexe), (head, host)):
        d = OUT / f"native_{cap.stem}_{who}"
        d.mkdir()
        grab = ["--capture-frames", shots, "--capture-dir", str(d)] if shots else ["--capture", str(d / "f.ppm"), "--capture-every", "1"]
        replay(f"native_{cap.stem}_{who}", cap, frames, native + grab, exe=exe)
        hashes[who] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in d.glob("*.ppm")}
        first = next(iter(sorted(d.glob("*.ppm"))), None)
        if first:   # (the frame's size, from the PPM header: 1280 896 expected)
            print(f"   {who}: {first.read_bytes()[:20].split()[1:3]}", flush=True)
    a, b = hashes["main"], hashes[head]
    same = sum(1 for n in a if b.get(n) == a[n])
    print(f"4. NATIVE {cap.stem}: main {len(a)} frames, {head} {len(b)}; identical {same}, differ "
          f"{sum(1 for n in a if n in b and b[n] != a[n])}, missing {len(set(a) ^ set(b))}: "
          f"{'pass' if a and same == len(a) == len(b) else 'FAIL'}", flush=True)
    for who in ("main", head):
        shutil.rmtree(OUT / f"native_{cap.stem}_{who}", ignore_errors=True)