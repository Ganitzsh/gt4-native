import os, subprocess, sys
WT = r"C:\Users\julie\gt4-wt-land"
cap = sys.argv[1]; frames = sys.argv[2]
sys.path.insert(0, r"C:\Users\julie\gt4-native-game\tools")
import slot; slot.kill_with_me()
r = subprocess.run([WT + r"\build\host-windows-Release\gt4_host.exe", "--headless", "--replay", r"C:\Users\julie\gt4-native-game\build\caps" + "\\" + cap,
                    "--frames", frames, "--fixed-step", "--size", "640x360", "--preset", "original", "--set", "render.scene=hybrid",
                    "--set", "lighting.night=enhanced", "--set", "texture.hd_pack=off"], capture_output=True, text=True, errors="replace",
                   creationflags=subprocess.CREATE_NO_WINDOW)
for l in (r.stdout + r.stderr).splitlines():
    low = l.lower()
    if ("light" in low or "lamp" in low or "night" in low) and "f2b glow class" not in low and "vulkan layers" not in low:
        print(l[:700])
