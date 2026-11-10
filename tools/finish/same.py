#!/usr/bin/env python3
"""same.py [--ref EXE] CASE...: the new host's frames against the reference host's, byte for byte, per case.

CASE: name=cap:frames:shots[:set,set...][:ENV=V,ENV=V...]   (shots: A-B/N or a list a,b,c; sets as k=v)
Prints identical / differ per case; exit 1 on any difference or missing frame. Also `val:` prefix on a name runs only
the new host with the Vulkan validation layers and counts their messages (0 expected).
"""
import hashlib
import os
import shutil
import subprocess
import sys
from pathlib import Path

WT = Path(r"C:\Users\julie\gt4-wt-land")
CAPS = Path(r"C:\Users\julie\gt4-native-game\build\caps")
HOST = WT / "build/host-windows-Release/gt4_host.exe"
REF = WT / "build/ref/gt4_host_main.exe"
OUT = WT / "build/same"


def run(exe, tag, cap, frames, shots, sets, envs):
    d = OUT / tag
    shutil.rmtree(d, ignore_errors=True)
    d.mkdir(parents=True)
    args = [str(exe), "--headless", "--replay", str(CAPS / cap), "--frames", frames, "--fixed-step", "--size", "1280x720",
            "--preset", "original", "--set", "display.frame_interpolation=off", "--set", "texture.hd_pack=off",
            "--set", "debug.overlay=off"]
    for s in sets:
        args += ["--set", s]
    args += ["--capture-frames", shots, "--capture-dir", str(d)]
    r = subprocess.run(args, capture_output=True, text=True, errors="replace", creationflags=subprocess.CREATE_NO_WINDOW,
                       env=dict(os.environ, **envs))
    hashes = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in d.glob("*.ppm")}
    shutil.rmtree(d, ignore_errors=True)
    return hashes, r.stdout + r.stderr, r.returncode


def main():
    sys.path.insert(0, r"C:\Users\julie\gt4-native-game\tools")
    import slot
    slot.kill_with_me()
    a = sys.argv[1:]
    ref = REF
    if a and a[0] == "--ref":
        ref = Path(a[1])
        a = a[2:]
    bad = 0
    for case in a:
        name, _, rest = case.partition("=")
        parts = rest.split(":")
        cap, frames, shots = parts[:3]
        sets = [s for s in (parts[3].split(",") if len(parts) > 3 else []) if s]
        envs = dict(kv.split("=", 1) for kv in (parts[4].split(",") if len(parts) > 4 else []) if kv)
        if name.startswith("val"):
            envs = dict(envs, VK_INSTANCE_LAYERS="VK_LAYER_KHRONOS_validation")
            h, text, code = run(HOST, name, cap, frames, shots, sets, envs)
            msgs = [l for l in text.splitlines() if l.startswith("Vulkan error") or l.startswith("Vulkan warning") or "VUID" in l]
            print(f"{name}: exit {code}, {len(h)} frames, {len(msgs)} Vulkan error/warning lines", flush=True)
            for m in msgs[:6]:
                print("   " + m[:400], flush=True)
            bad += bool(msgs) or code != 0
            continue
        ha, _, ca = run(ref, name + "_ref", cap, frames, shots, sets, envs)
        hb, _, cb = run(HOST, name + "_new", cap, frames, shots, sets, envs)
        same = sum(1 for n in ha if hb.get(n) == ha[n])
        ok = bool(ha) and same == len(ha) == len(hb) and ca == 0 and cb == 0
        print(f"{name}: ref {len(ha)} frames (exit {ca}), new {len(hb)} (exit {cb}); identical {same}, differ "
              f"{sum(1 for n in ha if n in hb and hb[n] != ha[n])}: {'pass' if ok else 'FAIL'}", flush=True)
        bad += not ok
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
