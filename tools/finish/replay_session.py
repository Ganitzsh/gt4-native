#!/usr/bin/env python3
"""A live B-spec night race to its end and its replay (lane A's a3b0.py to_replay route, r5 saves), headless.

    py -I replay_session.py --wt WORKTREE --host HOST --game GAME --out DIR [K=V ...]

K=V pairs go into the game's environment. The save is a fresh copy of the approved build/r5_saves, deleted at the
end. Run under tools/slot.py (a heavy slot); the host is stopped by its own PID. Stills are game content: local only.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import time

MAIN = r"C:\Users\julie\gt4-native-game"
ORIG = r"C:\Users\julie\gt4-decomp\orig\SCUS-97328"


class Session:
    def __init__(self, proc, session):
        self.proc, self.dir, self.n = proc, session, 0

    def queue(self, *commands):
        for c in commands:
            tmp = os.path.join(self.dir, "commands", f"{self.n:06d}.tmp")
            with open(tmp, "w") as f:
                f.write(c + "\n")
            os.replace(tmp, tmp[:-4] + ".cmd")
            self.n += 1

    def press(self, *pairs):
        for button, wait in zip(pairs[0::2], pairs[1::2]):
            self.queue(f"button {button} 1", "vwait 6", f"button {button} 0", f"vwait {wait}")

    def shot(self, name, deadline=400):
        self.queue(f"capture {name}")
        path = os.path.join(self.dir, "frames", name + ".ppm")
        end = time.time() + deadline
        while not os.path.exists(path):
            if self.proc.poll() is not None:
                raise SystemExit(f"the host ended (exit {self.proc.returncode}) before the shot {name}")
            if time.time() > end:
                raise SystemExit(f"no shot {name} after {deadline} s")
            time.sleep(0.5)
        time.sleep(0.5)   # (the file complete)
        return path


def route(s):
    from PIL import Image
    s.queue("vwait 1400")
    s.press("start", 600)
    s.shot("c_home")
    steps = [("triangle", 600), ("dpdown", 60), ("dpright", 60), ("cross", 500), ("cross", 500), ("cross", 600),
             ("dpright", 40), ("dpright", 40), ("dpright", 60), ("cross", 900), ("cross", 900), ("dpright", 60)]
    for k, (button, wait) in enumerate(steps):
        s.queue(f"button {button} 1", f"vwait {4 if button.startswith('dp') and wait == 60 and k < 3 else 6}", f"button {button} 0", f"vwait {wait}")
        if os.environ.get("ROUTE_DIAG"):
            s.shot(f"d{k:02d}_{button}")
    if os.environ.get("ROUTE_DIAG"):
        s.queue("quit")
        return
    s.shot("c_start")
    s.queue("button cross 1", "vwait 6", "button cross 0", "vwait 600")   # B-spec
    s.shot("c_race")
    s.queue("vwait 10500")
    seen_prize = False
    for i in range(6):
        s.press("cross", 600)
        img = Image.open(s.shot(f"r_wait{i}", 1200)).convert("L").resize((64, 36))
        panel = sum(img.getpixel((x, y)) for x in range(14, 34) for y in range(4, 24)) / 400
        print(f"after Cross {i + 1}: centre brightness {panel:.0f}", flush=True)
        if panel > 110:
            seen_prize = True
        elif seen_prize:
            break
    s.press("dpright", 90)
    s.shot("r_icon")
    s.press("cross", 900)
    for i in range(6):
        s.shot(f"replay{i}")
        s.queue("vwait 600")
    s.shot("replay_end")
    s.queue("quit")


def licence(s):
    """To the License Center and into a test, a shot after each press (ROUTE=licence; LICENCE_STEPS overrides)."""
    s.queue("vwait 1400")
    s.press("start", 600)
    default = "triangle:600,dpdown:60,cross:600,cross:600,cross:600,cross:900,cross:900,cross:900"
    for k, step in enumerate(os.environ.get("LICENCE_STEPS", default).split(",")):
        button, wait = step.split(":", 1)
        if button == "raw":       # raw:COMMAND TEXT
            s.queue(wait)
        elif button == "hold":    # hold:BUTTON:VBLANKS
            button, wait = wait.split(":")
            s.queue(f"button {button} 1", f"vwait {wait}", f"button {button} 0", "vwait 30")
        else:
            s.queue(f"button {button} 1", "vwait 6", f"button {button} 0", f"vwait {wait}")
        s.shot(f"l{k:02d}_{button}")
    s.queue("quit")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--wt", required=True)
    ap.add_argument("--host", required=True)
    ap.add_argument("--game", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--preset", default="high")
    ap.add_argument("--deadline", type=int, default=1800)
    ap.add_argument("envs", nargs="*")
    a = ap.parse_args()
    sys.path.insert(0, os.path.join(MAIN, "tools"))
    import slot
    slot.kill_with_me()
    shutil.rmtree(a.out, ignore_errors=True)
    session, saves = os.path.join(a.out, "session"), os.path.join(a.out, "saves")
    os.makedirs(os.path.join(session, "commands"))
    shutil.copytree(os.path.join(MAIN, "build", "r5_saves"), saves)
    env = dict(os.environ, GT4_ELF=os.path.join(ORIG, "CORE.GT4.elf"),
               GT4_ISO=os.path.join(ORIG, "Gran Turismo 4 (USA) (v1.01).iso"), **dict(kv.split("=", 1) for kv in a.envs))
    log = open(os.path.join(a.out, "host.log"), "wb")
    start = time.time()
    proc = subprocess.Popen([a.host, "--headless", "--no-audio", "--size", "960x540", "--session-dir", session, "--game", a.game,
                             "--saves", saves, "--preset", a.preset, "--set", "display.frame_interpolation=off",
                             "--set", "texture.hd_pack=off", "--set", "ui.entry=hotkey", "--set", "ui.pause=gt4"],
                            cwd=a.wt, env=env, stdout=log, stderr=subprocess.STDOUT,
                            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    code = "?"
    try:
        (licence if os.environ.get('ROUTE') == 'licence' else route)(Session(proc, session))
        try:
            code = proc.wait(timeout=max(60, a.deadline - (time.time() - start)))
        except subprocess.TimeoutExpired:
            code = "deadline"
    except SystemExit as e:
        code = f"route: {e}"
    finally:
        if proc.poll() is None:
            with open(os.path.join(session, "commands", "999999.cmd"), "w") as f:
                f.write("quit\n")
            try:
                proc.wait(timeout=60)
            except subprocess.TimeoutExpired:
                proc.kill()
        log.close()
        shutil.rmtree(saves, ignore_errors=True)
    print(f"== replay session: exit {code}, {time.time() - start:.0f} s", flush=True)
    for line in open(os.path.join(a.out, "host.log"), errors="replace"):
        if re.search(r"kernel: frame|kernel:   |waited over|camera cut|Camera cuts|Night side|N3 baked|Hybrid side|"
                     r"Vulkan (error|warning)|[Ee]rror|assert|crash|exception", line) and len(line) < 4000:
            print("   " + line.rstrip()[:300], flush=True)
    return 0 if code == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
