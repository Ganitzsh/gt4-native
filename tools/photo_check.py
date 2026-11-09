#!/usr/bin/env python3
"""A release check, run by hand before tagging: Photo Travel's photo is a picture, not a black screen.

    py -I tools/photo_check.py --host PATH/gt4_host.exe --game PATH/gt4_game.exe --orig DIR --saves DIR
                               [--work DIR] [--keep] [--no-slots] [--min-gb 7]

--orig is the folder with CORE.GT4.elf and the USA 1.01 disc image ("Gran Turismo 4 (USA) (v1.01).iso"); --saves a
save folder that boots into GT mode's Home with GT mode at its start (the machine's approved r5 save). The save is
copied into --work first and the copy deleted after: the folder given is never written.

The host runs headless at `original` with a session folder (gt4_host --session-dir: pad presses, waits and captures
as command files). From Home: Triangle opens the GT map, left, left, up x3 puts the cursor on Photo Travel, Cross
opens it, Cross picks Beacon Hill, the shooting screen loads; down, right x4 puts the cursor on Take photo, and
Cross takes it. The photo view shows the photo over its button row (Save / Return / Move / Zoom). The check
measures the mean brightness of the photo view above that row, prints it, and fails (exit 1) under
PHOTO_BRIGHTNESS_FLOOR. A black photo (0.1.0, and main until the CT24 read fix: the tiles read back from the memory
model, not the GPU) measures about 0; Beacon Hill's photo measures far above the floor. A photo still developing (a
slow host) is captured again, PHOTO_WAIT VBlanks apart, up to PHOTO_TRIES times; each value is printed.
The shooting screen's brightness is printed too: if it is low, the route went astray (another save, another map
layout) and the check says so instead of judging the photo.
"""
import argparse
import os
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

PHOTO_BRIGHTNESS_FLOOR = 20.0     # mean of R, G, B (0-255) over the photo view above its button row
SCREEN_BRIGHTNESS_FLOOR = 20.0    # the shooting screen (grey panels): under it, the route did not reach it
BUTTON_ROW = 0.88                 # the photo view's button row starts at this fraction of the height
PHOTO_TRIES, PHOTO_WAIT = 5, 600  # a photo under the floor is captured again, PHOTO_WAIT VBlanks apart


def tap(button, wait):
    return [f"button {button} 1", "vwait 6", f"button {button} 0", f"vwait {wait}"]


ROUTE = (["vwait 1400"] + tap("start", 600)              # the title, then GT mode's Home
         + tap("triangle", 180)                          # the GT map, the cursor on Home
         + tap("dpleft", 60) * 2 + tap("dpup", 60) * 3   # Photo Travel
         + tap("cross", 300) + tap("cross", 600)         # Photo Travel's list, Beacon Hill's shooting screen
         + tap("dpdown", 40) + tap("dpright", 40) * 4    # the cursor on Take photo
         + ["capture screen"] + tap("cross", 600)        # the shot, then the photo view
         + ["capture photo"])


def brightness(path, top=1.0):
    """The mean of R, G, B over the rows above `top` (a fraction of the height) of a P6 PPM, every 4th pixel."""
    data = open(path, "rb").read()
    # the header by hand: four tokens (P6, width, height, maxval), then exactly one whitespace byte; the pixel bytes
    # after it may themselves be whitespace values, which a split would eat
    tokens, at = [], 0
    while len(tokens) < 4:
        while data[at:at + 1].isspace():
            at += 1
        end = at
        while not data[end:end + 1].isspace():
            end += 1
        tokens.append(data[at:end])
        at = end
    w, h, pixels = int(tokens[1]), int(tokens[2]), data[at + 1:]
    total, n = 0, 0
    for y in range(0, int(h * top), 4):
        row = pixels[y * w * 3:(y + 1) * w * 3]
        for x in range(0, w * 3, 12):
            total += row[x] + row[x + 1] + row[x + 2]
            n += 3
    return total / n if n else 0.0


def wait_for(path, proc, seconds):
    end = time.time() + seconds
    while time.time() < end:
        if os.path.exists(path) and os.path.getsize(path) > 0:
            time.sleep(0.5)
            return True
        if proc.poll() is not None:
            return False
        time.sleep(0.5)
    return False


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--host", required=True)
    ap.add_argument("--game", required=True)
    ap.add_argument("--orig", required=True, help="the folder with CORE.GT4.elf and the USA 1.01 disc image")
    ap.add_argument("--saves", required=True, help="a save folder that boots into GT mode's Home (copied first)")
    ap.add_argument("--work", default=os.path.join(ROOT, "build", "photo_check"))
    ap.add_argument("--keep", action="store_true", help="keep the work folder (its captures and the host's log)")
    ap.add_argument("--no-slots", action="store_true", help="don't take this machine's heavy slot (tools/slot.py)")
    ap.add_argument("--min-gb", type=float, default=7.0)
    a = ap.parse_args()
    for k in ("host", "game", "orig", "saves", "work"):   # (relative to where it was run, not to the host's cwd)
        setattr(a, k, os.path.abspath(getattr(a, k)))
    if a.no_slots or not os.path.isdir(os.path.join(os.path.expanduser("~"), "locks")):
        return run(a)
    import slot as slots
    slots.kill_with_me()   # the host (and its game) end with this process, however it ends
    with slots.held(["heavy"], "photo_check: Photo Travel's photo", a.min_gb):   # released on any exit
        return run(a)


def run(a):
    shutil.rmtree(a.work, ignore_errors=True)
    session, saves = os.path.join(a.work, "session"), os.path.join(a.work, "saves")
    os.makedirs(os.path.join(session, "commands"))
    shutil.copytree(a.saves, saves)
    env = dict(os.environ, GT4_ELF=os.path.join(a.orig, "CORE.GT4.elf"),
               GT4_ISO=os.path.join(a.orig, "Gran Turismo 4 (USA) (v1.01).iso"))
    log = open(os.path.join(a.work, "host.log"), "wb")
    proc = subprocess.Popen([a.host, "--headless", "--no-audio", "--size", "640x448", "--session-dir", session,
                             "--game", a.game, "--saves", saves, "--preset", "original",
                             "--set", "display.frame_interpolation=off", "--set", "texture.hd_pack=off"],
                            cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    try:
        for k, c in enumerate(ROUTE):   # each written whole before it is visible, in order of their names
            tmp = os.path.join(session, "commands", f"{k:06d}.tmp")
            with open(tmp, "w") as f:
                f.write(c + "\n")
            os.replace(tmp, tmp[:-4] + ".cmd")
        frames = os.path.join(session, "frames")
        if not wait_for(os.path.join(frames, "screen.ppm"), proc, 900):
            print("photo_check: FAIL: the shooting screen was never captured (the host's log: "
                  f"{os.path.join(a.work, 'host.log')})")
            return 1
        screen = brightness(os.path.join(frames, "screen.ppm"))
        # The shot develops its photo tile by tile, each a read the game waits for; a slow host (a loaded machine:
        # reads of 30-500 ms) can still be developing it when the route's first capture is taken, while VBlanks run
        # on. So under the floor, the photo view is captured again PHOTO_WAIT VBlanks later, up to PHOTO_TRIES times.
        photos = []
        for k in range(PHOTO_TRIES):
            name = "photo" if k == 0 else f"photo{k}"
            if k:
                for j, c in enumerate((f"vwait {PHOTO_WAIT}", f"capture {name}")):
                    tmp = os.path.join(session, "commands", f"{100000 + 10 * k + j:06d}.tmp")
                    with open(tmp, "w") as f:
                        f.write(c + "\n")
                    os.replace(tmp, tmp[:-4] + ".cmd")
            if not wait_for(os.path.join(frames, name + ".ppm"), proc, 600):
                print("photo_check: FAIL: the photo view was never captured")
                return 1
            photos.append(brightness(os.path.join(frames, name + ".ppm"), BUTTON_ROW))
            if photos[-1] >= PHOTO_BRIGHTNESS_FLOOR:
                break
        photo = photos[-1]
        print(f"photo_check: shooting screen {screen:.1f} (floor {SCREEN_BRIGHTNESS_FLOOR}), "
              f"photo {photo:.1f} (floor {PHOTO_BRIGHTNESS_FLOOR}; captures "
              f"{', '.join(f'{p:.1f}' for p in photos)}, {PHOTO_WAIT} VBlanks apart)")
        if screen < SCREEN_BRIGHTNESS_FLOOR:
            print("photo_check: FAIL: the route did not reach the shooting screen (another save or map layout?)")
            return 1
        if photo < PHOTO_BRIGHTNESS_FLOOR:
            print("photo_check: FAIL: Photo Travel's photo is black")
            return 1
        print("photo_check: OK")
        return 0
    finally:
        with open(os.path.join(session, "commands", "999999.cmd"), "w") as f:
            f.write("quit\n")
        try:
            proc.wait(timeout=120)
        except subprocess.TimeoutExpired:
            proc.kill()
        log.close()
        shutil.rmtree(saves, ignore_errors=True)   # the save's copy, always
        if not a.keep:
            shutil.rmtree(a.work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
