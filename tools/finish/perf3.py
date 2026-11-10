"""Day cost of hybrid: py -I perf3.py HOST CAP FRAMES PRESET NAME[=K=V,...] ...   (NAME: gs or hybrid)"""
import os, subprocess, sys
CAPS = r"C:\Users\julie\gt4-native-game\build\caps"
sys.path.insert(0, r"C:\Users\julie\gt4-native-game\tools")
import slot; slot.kill_with_me()
host, cap, frames, preset = sys.argv[1:5]
out = os.path.join(os.path.dirname(os.path.dirname(host)), "perf"); os.makedirs(out, exist_ok=True)
runs = {"gs": ["--set", "render.scene=original"], "hybrid": ["--set", "render.scene=hybrid"]}
for spec in sys.argv[5:]:
    name, _, envs = spec.partition("=")
    env = dict(os.environ, **dict(kv.split("=", 1) for kv in envs.split(",") if kv))
    for rep in range(2):
        r = subprocess.run([host, "--headless", "--replay", os.path.join(CAPS, cap), "--frames", frames,
                            "--fixed-step", "--size", "1920x1080", "--preset", preset, "--set", "display.frame_interpolation=off",
                            "--set", "texture.hd_pack=off", *runs[name]],
                           capture_output=True, text=True, errors="replace", env=env, creationflags=subprocess.CREATE_NO_WINDOW)
        lines = (r.stdout + r.stderr).splitlines()
        keep = [l for l in lines if "CPU a frame" in l or "GPU" in l and "ms" in l and "frame" in l]
        print(spec, rep, "exit", r.returncode, flush=True)
        for l in keep[-3:]:
            print("   ", l[-230:], flush=True)
