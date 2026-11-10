import os, subprocess, sys
WT = r"C:\Users\julie\gt4-wt-land"
CAPS = r"C:\Users\julie\gt4-native-game\build\caps"
sys.path.insert(0, r"C:\Users\julie\gt4-native-game\tools")
import slot; slot.kill_with_me()
cap, frames, preset = sys.argv[1], sys.argv[2], sys.argv[3]
out = os.path.join(WT, "build", "perf"); os.makedirs(out, exist_ok=True)
runs = {"base": [], "hybrid": ["--set", "render.scene=hybrid"], "modern_night": ["--set", "render.scene=hybrid", "--set", "lighting.night=enhanced"],
        "night_gs": ["--set", "lighting.night=enhanced"]}
for name in sys.argv[4:]:
    path = os.path.join(out, f"{name}_{preset}.jsonl")
    if os.path.exists(path): os.remove(path)
    r = subprocess.run([WT + r"\build\host-windows-Release\gt4_host.exe", "--headless", "--replay", os.path.join(CAPS, cap), "--frames", frames,
                        "--fixed-step", "--size", "1920x1080", "--preset", preset, "--set", "display.frame_interpolation=off",
                        "--set", "texture.hd_pack=off", *runs[name], "--profile-json", path],
                       capture_output=True, text=True, errors="replace", creationflags=subprocess.CREATE_NO_WINDOW)
    cpu = [l for l in (r.stdout + r.stderr).splitlines() if "CPU a frame" in l]
    print(name, "exit", r.returncode, (cpu[-1][cpu[-1].index("CPU a frame"):][:120] if cpu else ""), flush=True)
