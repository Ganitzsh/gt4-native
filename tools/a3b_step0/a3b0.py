#!/usr/bin/env python3
"""A3b step 0 (debug, never for main: debug/a-a3b-step0, 5d62ed8). The game of build/review/wa3b built into
build/game_a3b (checked: the debug copy linked), a host of main (review/host-main) built from an empty folder, then
sessions whose 'a3b' lines (the race camera object and entry per frame; each main view's camera position, direction
and projection scale per VBlank) go to build/review/a3b_<session>.txt:
  s1  a night B-spec race (C.S. Route 5 Reverse, Sunday Cup race 4) from the start sequence: Cross (change view)
      every 300 VBlanks, ten times, a shot after each; then a pause and resume (Start, 180, Start)
A fresh copy of saves-ss-r5's saves/, deleted after. No shell."""
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

A = Path(r"C:\Users\julie\gt4-lane-A")
R, R5 = A / "build" / "review", A / "build" / "r5tools"
GAME, HOST = A / "build" / "game_a3b", R / "host-main"
MAIN = "2472d3e"
PY = sys.executable
sys.path.insert(0, str(A / "build" / "slot"))
sys.path.insert(0, str(R5))
import slot  # noqa: E402

if not (GAME / "gt4_game.exe").exists():
    with slot.held(["game"], "gt4-lane-A A3b step 0 debug game (camera logs; never for main)", 7.0):
        with open(R / "a3b_build.log", "w") as f:
            r = subprocess.run([PY, "tools/release_game.py", "--decomp", r"C:\Users\julie\gt4-decomp", "--jobs", "4",
                                "--out", str(GAME)], cwd=R / "wa3b", stdout=f, stderr=subprocess.STDOUT,
                               env=dict(os.environ, GT4_FFMPEG=str(A / "build" / "ffmpeg-i686")))
        print("release_game exit", r.returncode, flush=True)
        if r.returncode:
            raise SystemExit(1)
cache = json.load(open(GAME / "gen" / "nm_cache.json"))
cur = [v[2] for k, v in cache.items() if "native_curated" in k and k.endswith("func_0033EAB8.cpp.o")]
if not cur or not any("logf" in s for s in cur[0]):
    raise SystemExit("the debug 0033EAB8 is not the linked one")
if not (HOST / "gt4_host.exe").exists():
    w = R / "wa3bh"
    subprocess.run(["git", "-C", str(A), "worktree", "add", "-f", str(w), MAIN], check=True)
    try:
        with slot.held(["heavy"], f"gt4-lane-A A3b step 0: host of main {MAIN}", 7.0):
            with open(R / "a3b_host.log", "w") as f:
                subprocess.run(["cmd", "/c", str(R / "build_one.cmd"), str(w), str(HOST)], stdout=f, stderr=subprocess.STDOUT)
        print("host:", [l.strip() for l in open(R / "a3b_host.log", errors="replace") if "tests passed" in l], flush=True)
    finally:
        subprocess.run(["git", "-C", str(A), "worktree", "remove", "--force", str(w)])

os.environ["GT4_HOST_RECORD_SCENE"] = "1"   # (scene records on: runtime/view_begin logs only with them)
os.environ["R5_GAME"] = str(GAME / "gt4_game.exe")
os.environ["R5_HOST"] = str(HOST / "gt4_host.exe")
import r5_session as r5  # noqa: E402


def s1(s):
    s.queue("vwait 1400")
    s.press("start", 600)
    if r5.is_home(s.shot("c_home")):
        s.press("triangle", 600)
    for m in ("dpdown", "dpright"):
        s.queue(f"button {m} 1", "vwait 4", f"button {m} 0", "vwait 60")
    s.press("cross", 500)
    s.press("cross", 500, "cross", 600)
    s.press("dpright", 40, "dpright", 40, "dpright", 60)
    s.press("cross", 900, "cross", 900, "cross", 900)
    s.queue("button dpright 1", "vwait 6", "button dpright 0", "vwait 60")
    (s.dir / "mark_start").write_text("start sequence\n")
    s.queue("button cross 1", "vwait 6", "button cross 0", "vwait 1500")   # B-spec: the start sequence, the race
    s.shot("race")
    for i in range(10):
        s.queue("button cross 1", "vwait 6", "button cross 0", "vwait 300")   # change view
        s.shot(f"view{i}")
    s.queue("button start 1", "vwait 6", "button start 0", "vwait 180")       # pause
    s.shot("paused")
    s.queue("button cross 1", "vwait 6", "button cross 0", "vwait 300")       # resume: CONTINUE
    s.shot("resumed")
    for i in range(10, 14):                                                    # more views after the pause
        s.queue("button cross 1", "vwait 6", "button cross 0", "vwait 300")
        s.shot(f"view{i}")


def s2(s):
    """A-spec on the same event: our car driven (R2 held), Select (a guess for A-spec's view button) every 300."""
    s.queue("vwait 1400")
    s.press("start", 600)
    if r5.is_home(s.shot("c_home")):
        s.press("triangle", 600)
    for m in ("dpdown", "dpright"):
        s.queue(f"button {m} 1", "vwait 4", f"button {m} 0", "vwait 60")
    s.press("cross", 500)
    s.press("cross", 500, "cross", 600)
    s.press("dpright", 40, "dpright", 40, "dpright", 60)
    s.press("cross", 900, "cross", 900, "cross", 900)
    s.shot("race_screen")
    s.queue("button cross 1", "vwait 6", "button cross 0", "vwait 1500")   # A-spec (the default): the race
    s.shot("race")
    s.queue("axis righttrigger 32767")
    for i in range(6):
        s.queue("button select 1", "vwait 6", "button select 0", "vwait 300")
        s.shot(f"aview{i}")
    s.queue("axis righttrigger 0", "vwait 120")
    s.shot("stopped")


def s3(s):
    """A B-spec race to its end (exploring the way to its replay): a shot every 1,800 VBlanks for up to 24,000, then
    Cross with a shot after each, to read the post-race menus."""
    s.queue("vwait 1400")
    s.press("start", 600)
    if r5.is_home(s.shot("c_home")):
        s.press("triangle", 600)
    for m in ("dpdown", "dpright"):
        s.queue(f"button {m} 1", "vwait 4", f"button {m} 0", "vwait 60")
    s.press("cross", 500)
    s.press("cross", 500, "cross", 600)
    s.press("dpright", 40, "dpright", 40, "dpright", 60)
    s.press("cross", 900, "cross", 900, "cross", 900)
    s.queue("button dpright 1", "vwait 6", "button dpright 0", "vwait 60")
    s.queue("button cross 1", "vwait 6", "button cross 0", "vwait 600")   # B-spec
    for i in range(14):
        s.queue("vwait 1800")
        s.shot(f"lap{i:02d}")
    for i in range(6):
        s.press("cross", 400)
        s.shot(f"post{i}")


def s4(s):
    """The B-spec race to its end, then the Result screen's icons read one by one (a shot of each label), then the
    second icon (a guess for Replay) and 2,400 VBlanks of what it plays, with shots."""
    s.queue("vwait 1400")
    s.press("start", 600)
    if r5.is_home(s.shot("c_home")):
        s.press("triangle", 600)
    for m in ("dpdown", "dpright"):
        s.queue(f"button {m} 1", "vwait 4", f"button {m} 0", "vwait 60")
    s.press("cross", 500)
    s.press("cross", 500, "cross", 600)
    s.press("dpright", 40, "dpright", 40, "dpright", 60)
    s.press("cross", 900, "cross", 900, "cross", 900)
    s.queue("button dpright 1", "vwait 6", "button dpright 0", "vwait 60")
    s.queue("button cross 1", "vwait 6", "button cross 0", "vwait 600")   # B-spec
    s.queue("vwait 10500")                                                  # (the race: about 2'36", then the results)
    s.shot("finish")
    for i in range(2):     # results, prize OK: the Result menu (Retry first; a third Cross takes Retry, s4 showed)
        s.press("cross", 500)
        s.shot(f"after{i}")
    for i in range(1, 6):
        s.press("dpright", 90)
        s.shot(f"icon{i}")
    for _ in range(4):
        s.press("dpleft", 60)
    s.shot("icon_choice")                                                   # (the second icon)
    s.press("cross", 900)
    s.shot("replay0")
    for i in range(1, 9):
        s.queue("vwait 300")
        s.shot(f"replay{i}")


def tap(s, button, wait):
    s.queue(f"button {button} 1", "vwait 6", f"button {button} 0", f"vwait {wait}")


def s5(s):
    """Photo Travel's preview (lane C's route, tools/photo_check.py on main; r5 saves boot at Home): Beacon Hill's
    shooting screen, then the camera moved with the sticks, each fully one way for 120 VBlanks then back, then both."""
    s.queue("vwait 1400")
    tap(s, "start", 600)
    tap(s, "triangle", 180)
    for _ in range(2):
        tap(s, "dpleft", 60)
    for _ in range(3):
        tap(s, "dpup", 60)
    tap(s, "cross", 300)
    tap(s, "cross", 600)
    s.shot("pt_screen")
    for axis in ("leftx", "lefty", "rightx", "righty"):
        for value in (32767, -32768):
            s.queue(f"axis {axis} {value}", "vwait 120", f"axis {axis} 0", "vwait 60")
            s.shot(f"pt_{axis}_{'p' if value > 0 else 'n'}")
    s.queue("axis leftx 32767", "axis rightx 32767", "vwait 180", "axis leftx 0", "axis rightx 0", "vwait 60")
    s.shot("pt_both")


def s6(s):
    """s5's route, then the camera moved as the screen's prompt says: Cross held with the left stick (pan and tilt),
    each direction for 120 VBlanks; then L1 and R1 (zoom) held 120 VBlanks each; then a fast pan, the stick fully
    one way and back within 20 VBlanks (the case a camera-cut test must not flag)."""
    s.queue("vwait 1400")
    tap(s, "start", 600)
    tap(s, "triangle", 180)
    for _ in range(2):
        tap(s, "dpleft", 60)
    for _ in range(3):
        tap(s, "dpup", 60)
    tap(s, "cross", 300)
    tap(s, "cross", 600)
    s.shot("pt6_screen")
    for axis in ("leftx", "lefty"):
        for value in (32767, -32768):
            s.queue("button cross 1", "vwait 10", f"axis {axis} {value}", "vwait 120", f"axis {axis} 0", "vwait 10",
                    "button cross 0", "vwait 60")
            s.shot(f"pt6_{axis}_{'p' if value > 0 else 'n'}")
    for b in ("l1", "r1"):
        s.queue(f"button {b} 1", "vwait 120", f"button {b} 0", "vwait 60")
        s.shot(f"pt6_{b}")
    s.queue("button cross 1", "vwait 10", "axis leftx 32767", "vwait 10", "axis leftx -32768", "vwait 10",
            "axis leftx 0", "vwait 10", "button cross 0", "vwait 60")
    s.shot("pt6_fast")


def to_replay(s):
    """s4's route: the B-spec race to its end, two Crosses to the Result menu, then the second icon (Replay)."""
    s.queue("vwait 1400")
    s.press("start", 600)
    if r5.is_home(s.shot("c_home")):
        s.press("triangle", 600)
    for m in ("dpdown", "dpright"):
        s.queue(f"button {m} 1", "vwait 4", f"button {m} 0", "vwait 60")
    s.press("cross", 500)
    s.press("cross", 500, "cross", 600)
    s.press("dpright", 40, "dpright", 40, "dpright", 60)
    s.press("cross", 900, "cross", 900, "cross", 900)
    s.queue("button dpright 1", "vwait 6", "button dpright 0", "vwait 60")
    s.queue("button cross 1", "vwait 6", "button cross 0", "vwait 600")   # B-spec
    s.queue("vwait 10500")
    # Crosses until the Result menu. First the Results list (dark, centre about 25), then the prize dialog (a white
    # panel, about 175), then the Result menu (dark, about 31): stop at the first dark screen after a bright one.
    # How many Crosses depends on the timing (s4 took 2 with shots between them; s7's first run needed 3).
    from PIL import Image
    seen_prize = False
    for i in range(6):
        s.press("cross", 600)
        img = Image.open(s.shot(f"r_wait{i}")).convert("L").resize((64, 36))
        panel = sum(img.getpixel((x, y)) for x in range(14, 34) for y in range(4, 24)) / 400
        r5.log(f"after Cross {i + 1}: centre brightness {panel:.0f}")
        if panel > 110:
            seen_prize = True
        elif seen_prize:
            break
    s.press("dpright", 90)
    s.shot("r_icon")
    s.press("cross", 900)


def s7(s):
    """Photo Mode from a replay, step 1: the replay paused with Start 600 VBlanks in, its menu read (a shot after
    each cursor move down and right, nothing confirmed)."""
    to_replay(s)
    s.queue("vwait 600")
    s.shot("rp_playing")
    s.press("start", 120)
    s.shot("rp_pause")
    for i in range(1, 6):
        s.press("dpdown", 60)
        s.shot(f"rp_down{i}")
    for i in range(1, 6):
        s.press("dpright", 60)
        s.shot(f"rp_right{i}")


def s8(s):
    """The live replay's buttons one at a time (lane C: GT4's replays use them for views and the HUD), 300 VBlanks
    apart with a shot after each: Select, Triangle, Square, L1, R1, then the right stick each way. Not Start (it ends
    the replay, s7) nor Circle (likely back)."""
    to_replay(s)
    s.queue("vwait 300")
    s.shot("rb_playing")
    for i, b in enumerate(("select", "triangle", "square", "l1", "r1", "select", "triangle")):
        s.press(b, 300)
        s.shot(f"rb{i}_{b}")
    for axis, value in (("rightx", 32767), ("rightx", -32768), ("righty", 32767), ("righty", -32768)):
        s.queue(f"axis {axis} {value}", "vwait 90", f"axis {axis} 0", "vwait 200")
        s.shot(f"rb_{axis}_{'p' if value > 0 else 'n'}")


SAVES = {"s5": "r5_saves", "s6": "r5_saves"}   # (the others: saves-ss-r5's saves/)

for name, body in (("s1", s1), ("s2", s2), ("s3", s3), ("s4", s4), ("s5", s5), ("s6", s6), ("s7", s7), ("s8", s8)):
    if len(sys.argv) > 1 and name not in sys.argv[1:]:
        continue
    saves = A / "build" / f"a3b_{name}_saves"
    with slot.held(["heavy"], f"gt4-lane-A A3b step 0 session {name} (debug game)", 7.0):
        shutil.rmtree(saves, ignore_errors=True)
        shutil.copytree(A / "build" / SAVES[name] if name in SAVES else A / "build" / "saves-ss-r5" / "saves", saves)
        s = None
        try:
            s = r5.Session(f"a3b{name}", "640x360", None, str(saves))
            body(s)
        finally:
            if s:
                s.quit()
            shutil.rmtree(saves, ignore_errors=True)
    lines = [l for l in (A / "build" / "sessions" / f"a3b{name}" / "host.log").read_text(errors="replace").splitlines()
             if "a3b " in l]
    (R / f"a3b_{name}.txt").write_text("\n".join(lines) + "\n")
    print(name, len(lines), "a3b lines", flush=True)
print("done")
