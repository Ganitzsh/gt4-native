#!/usr/bin/env python3
"""val_diff.py CASE...: the validation layers' distinct messages, the new host against the reference host.

CASE as same.py's (name=cap:frames:shots[:sets][:envs]). Prints the messages only the new host gives (0 expected) and
how many both give (the reference's own: not this change's).
"""
import os
import re
import subprocess
import sys
from pathlib import Path

WT = Path(r"C:\Users\julie\gt4-wt-land")
CAPS = Path(r"C:\Users\julie\gt4-native-game\build\caps")


def messages(exe, cap, frames, sets, envs):
    args = [str(exe), "--headless", "--replay", str(CAPS / cap), "--frames", frames, "--fixed-step", "--size", "1280x720",
            "--preset", "original", "--set", "display.frame_interpolation=off", "--set", "texture.hd_pack=off"]
    for s in sets:
        args += ["--set", s]
    r = subprocess.run(args, capture_output=True, text=True, errors="replace", creationflags=subprocess.CREATE_NO_WINDOW,
                       env=dict(os.environ, VK_INSTANCE_LAYERS="VK_LAYER_KHRONOS_validation", **envs))
    out = set()
    for l in (r.stdout + r.stderr).splitlines():
        if l.startswith("Vulkan error") or l.startswith("Vulkan warning"):
            out.add(re.sub(r"0x[0-9a-fA-F]+", "0x", l)[:600])   # (handles differ run to run)
    return out, r.returncode


def main():
    sys.path.insert(0, r"C:\Users\julie\gt4-native-game\tools")
    import slot
    slot.kill_with_me()
    bad = 0
    for case in sys.argv[1:]:
        name, _, rest = case.partition("=")
        parts = rest.split(":")
        cap, frames = parts[0], parts[1]
        sets = [s for s in (parts[3].split(",") if len(parts) > 3 else []) if s]
        envs = dict(kv.split("=", 1) for kv in (parts[4].split(",") if len(parts) > 4 else []) if kv)
        new, cn = messages(WT / "build/host-windows-Release/gt4_host.exe", cap, frames, sets, envs)
        ref, cr = messages(WT / "build/ref/gt4_host_main.exe", cap, frames, sets, envs)
        only = sorted(new - ref)
        print(f"{name}: new {len(new)} distinct messages (exit {cn}), reference {len(ref)} (exit {cr}); only the new host: "
              f"{len(only)}; errors among the new host's: {sum(1 for m in new if m.startswith('Vulkan error'))}", flush=True)
        for m in only[:8]:
            print("   NEW: " + m[:500], flush=True)
        bad += bool(only)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
