#!/usr/bin/env python3
"""Route 5 night sessions for ticket N0, in one process (no shell loops: the machine leaks kernel tokens per
MSYS fork). Takes a machine-wide heavy slot (C:/Users/julie/locks/heavy-1|2, >= 6 GB free), starts the headless
host with the release game on a fresh copy of the r5 save, drives the menus to the grid of Clubman Stage Route 5
at night (Arcade, Time Trial, Ford Ka '01), then runs the requested job and quits.

    py r5_session.py scan NAME [--size 640x360] [--seconds 60]
        hold the accelerator and capture every presented frame (bursts of 600)
    py r5_session.py record NAME --cap CAPNAME --count N [--after VBLANKS] [--size ...]
        hold the accelerator, start GT4_HOST_RECORD after VBLANKS (trigger file), N VBlanks, low-rate frames
"""
import argparse
import ctypes
import datetime
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

from PIL import Image, ImageChops, ImageStat

CLONE = Path("C:/Users/julie/gt4-lane-F")
HERE = Path(__file__).resolve().parent
LOCKS = Path("C:/Users/julie/locks")
DECOMP = Path("C:/Users/julie/gt4-decomp/orig/SCUS-97328")
INFO = HERE / "info"   # reference crops of each course's "Course length / Elevation change" line (game data: local)


class MEMORYSTATUSEX(ctypes.Structure):
    _fields_ = [("dwLength", ctypes.c_ulong), ("dwMemoryLoad", ctypes.c_ulong),
                ("ullTotalPhys", ctypes.c_ulonglong), ("ullAvailPhys", ctypes.c_ulonglong),
                ("ullTotalPageFile", ctypes.c_ulonglong), ("ullAvailPageFile", ctypes.c_ulonglong),
                ("ullTotalVirtual", ctypes.c_ulonglong), ("ullAvailVirtual", ctypes.c_ulonglong),
                ("ullAvailExtendedVirtual", ctypes.c_ulonglong)]


def free_gb():
    m = MEMORYSTATUSEX()
    m.dwLength = ctypes.sizeof(m)
    ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(m))
    return m.ullAvailPhys / 2**30


def game_running():
    """Any gt4_game.exe on the machine: the game refuses a second instance (shared guest memory)."""
    r = subprocess.run(["tasklist", "/FI", "IMAGENAME eq gt4_game.exe", "/NH"], capture_output=True, text=True,
                       creationflags=subprocess.CREATE_NO_WINDOW)
    return "gt4_game.exe" in r.stdout


def _mkdir_lock(name, what):
    path = LOCKS / name
    try:
        path.mkdir()
    except FileExistsError:
        return None
    (path / "owner.txt").write_text(
        f"gt4-lane-F 4fcdf3f7 {what} {datetime.datetime.now().astimezone().isoformat(timespec='seconds')}\n")
    return path


def take_slot(what, live=False):
    """A heavy slot through the shared runner tools/slot.py (lane C, 2a189dd: it removes locks whose holder died, and
    writes the holder's pid); with live, live-game as well. Returns the locks taken (rmtree them to give back)."""
    sys.path.insert(0, str(CLONE / "tools"))
    import slot  # noqa: E402
    kind = os.environ.get("LANEF_SLOT", "heavy")   # heavy-both for a recording over 2 GB
    paths = slot.take([kind] + (["live-game"] if live else []), what, min_gb=7.0)   # (the coordinator's floor for all)
    log(f"slot {', '.join(os.path.basename(p) for p in paths)}, {free_gb():.1f} GB free")
    return paths

class _JobLimits(ctypes.Structure):
    _fields_ = [("PerProcessUserTimeLimit", ctypes.c_int64), ("PerJobUserTimeLimit", ctypes.c_int64),
                ("LimitFlags", ctypes.c_uint32), ("MinimumWorkingSetSize", ctypes.c_size_t),
                ("MaximumWorkingSetSize", ctypes.c_size_t), ("ActiveProcessLimit", ctypes.c_uint32),
                ("Affinity", ctypes.c_size_t), ("PriorityClass", ctypes.c_uint32), ("SchedulingClass", ctypes.c_uint32),
                ("IoCounters", ctypes.c_uint64 * 6), ("ProcessMemoryLimit", ctypes.c_size_t),
                ("JobMemoryLimit", ctypes.c_size_t), ("PeakProcessMemoryUsed", ctypes.c_size_t),
                ("PeakJobMemoryUsed", ctypes.c_size_t)]


def kill_on_close_job(process):
    """A job object holding `process` and everything it starts (the game): closing the job's handle, or this Python
    process dying, kills them all, so a game can never outlive its host (it would hold the guest lock)."""
    k32 = ctypes.windll.kernel32
    k32.CreateJobObjectW.restype = ctypes.c_void_p
    job = k32.CreateJobObjectW(None, None)
    limits = _JobLimits()
    limits.LimitFlags = 0x2000   # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
    if not job or not k32.SetInformationJobObject(ctypes.c_void_p(job), 9, ctypes.byref(limits), ctypes.sizeof(limits)):
        raise OSError("job object: " + str(ctypes.GetLastError()))
    if not k32.AssignProcessToJobObject(ctypes.c_void_p(job), ctypes.c_void_p(int(process._handle))):
        raise OSError("job object assign: " + str(ctypes.GetLastError()))
    return job


def _resume(process):
    """Resume a process started with CREATE_SUSPENDED (Popen keeps only the process handle)."""
    if ctypes.windll.ntdll.NtResumeProcess(ctypes.c_void_p(int(process._handle))) != 0:
        raise OSError("NtResumeProcess failed")


def log(text):
    print(f"[{time.strftime('%H:%M:%S')}] {text}", flush=True)


class Session:
    def __init__(self, name, size, record=None, saves_dir=None):
        self.dir = CLONE / "build" / "sessions" / name
        shutil.rmtree(self.dir, ignore_errors=True)
        (self.dir / "commands").mkdir(parents=True)
        if saves_dir:
            saves = Path(saves_dir)   # used in place: the game's own saves land there
        else:
            saves = CLONE / "build" / "r5_run_saves"
            shutil.rmtree(saves, ignore_errors=True)
            shutil.copytree(CLONE / "build" / "r5_saves", saves)
        env = dict(os.environ, GT4_ELF=str(DECOMP / "CORE.GT4.elf"),
                   GT4_ISO=str(DECOMP / "Gran Turismo 4 (USA) (v1.01).iso"))
        if record:
            cap, count = record
            (CLONE / "build" / "caps").mkdir(parents=True, exist_ok=True)
            path = CLONE / 'build' / 'caps' / (cap + '.cap')
            # LANEF_RECORD_FROM_BOOT=1: from game start (C10: VBlanks 0..COUNT), else from the trigger file
            env["GT4_HOST_RECORD"] = (f"{path},0,{count}" if os.environ.get("LANEF_RECORD_FROM_BOOT")
                                      else f"{path},trigger:{self.dir / 'rec_go'},{count}")
        # a copy of the host, so a build of build/host-windows-Release never meets a running exe
        run = CLONE / "build" / "lanef_run"
        run.mkdir(parents=True, exist_ok=True)
        host = run / f"gt4_host_{name}.exe"
        # LANEF_HOST=<exe>: a given host (it must match the game's link version), else the build folder's
        shutil.copy2(os.environ.get("LANEF_HOST", str(CLONE / "build/host-windows-Release/gt4_host.exe")), host)
        args = [str(host), "--headless", "--size", size,
                "--session-dir", str(self.dir), "--no-audio",
                "--game", os.environ.get("LANEF_GAME", str(CLONE / "build/game_release/gt4_game.exe")),
                "--saves", str(saves)]
        # LANEF_SETTINGS=<ini>: a settings file (a copy; the host may save to it) in place of original's defaults
        if os.environ.get("LANEF_SETTINGS"):
            args += ["--settings", os.environ["LANEF_SETTINGS"]]
        else:
            args += ["--preset", "original", "--set", "display.frame_interpolation=off", "--set", "texture.hd_pack=off"]
        self.logfile = open(self.dir / "host.log", "wb")
        # no console for the child (conhost is a suspected kernel-object leak on this machine)
        # suspended until it is in the job, so the game it starts is always in the job too
        self.proc = subprocess.Popen(args, cwd=CLONE, env=env, stdout=self.logfile, stderr=subprocess.STDOUT,
                                     creationflags=subprocess.CREATE_NO_WINDOW | 0x4)   # CREATE_SUSPENDED
        self.job = kill_on_close_job(self.proc)
        _resume(self.proc)
        self.seq = 0
        while not (self.dir / "events.jsonl").exists() or b'"frame"' not in (self.dir / "events.jsonl").read_bytes()[:20000]:
            if self.proc.poll() is not None:
                raise SystemExit("the host exited at start: see " + str(self.dir / "host.log"))
            time.sleep(1)

    def queue(self, *commands):
        """All at once, in order (sorted names), each written whole before it is visible."""
        base = time.time_ns()
        for c in commands:
            name = f"{base + self.seq:020d}-{self.seq:08x}"
            self.seq += 1
            tmp = self.dir / "commands" / (name + ".tmp")
            tmp.write_text(c + "\n", encoding="utf-8")
            tmp.rename(self.dir / "commands" / (name + ".cmd"))

    def press(self, *steps):
        """press(button, wait, button, wait, ...): each held 6 VBlanks, then the wait."""
        cmds = []
        for b, w in zip(steps[::2], steps[1::2]):
            cmds += [f"button {b} 1", "vwait 6", f"button {b} 0", f"vwait {w}"]
        self.queue(*cmds)

    def shot(self, name, timeout=600):
        self.queue(f"capture {name}")
        path = self.dir / "frames" / (name + ".ppm")
        t0 = time.time()
        while not path.exists() or path.stat().st_size == 0:
            if self.proc.poll() is not None:
                raise SystemExit("the host exited")
            if time.time() - t0 > timeout:
                raise SystemExit(f"no capture {name}")
            time.sleep(0.5)
        time.sleep(0.5)
        return path

    def quit(self):
        self.queue("quit")
        try:
            self.proc.wait(timeout=120)
        except subprocess.TimeoutExpired:
            self.proc.kill()
        # the host is gone; whatever it started goes with the job (a game that never linked stays otherwise)
        ctypes.windll.kernel32.TerminateJobObject(ctypes.c_void_p(self.job), 1)
        ctypes.windll.kernel32.CloseHandle(ctypes.c_void_p(self.job))
        self.logfile.close()


def read_ppm(path):
    data = path.read_bytes()
    parts = data.split(maxsplit=4)
    w, h = int(parts[1]), int(parts[2])
    return Image.frombytes("RGB", (w, h), parts[4][: w * h * 3])


CITY_ORDER = ["amalfi", "georgev", "opera", "hongkong", "citta", "newyork", "tokyo", "cote", "ssr5", "csr5",
              "seattle", "seoul"]   # the City Courses row with every course open (saves-ss-r5)


def which_course(path, refs=None):
    shot = read_ppm(path).resize((1280, 720)).crop((300, 668, 980, 708)).convert("L")
    scores = sorted((ImageStat.Stat(ImageChops.difference(shot, Image.open(f))).mean[0], f.stem)
                    for f in (refs or INFO).glob("*.png"))
    ok = scores[0][0] < 12 and scores[1][0] - scores[0][0] > 2
    return scores[0][1] if ok else "unknown"


def is_home(path):
    """GT Home has a dark red title bar at the top left ("<name>'s Home"); the GT map is light grey there."""
    im = read_ppm(path).resize((1280, 720)).crop((480, 5, 620, 20))   # the bar above the title, beside the text
    r, g, b = ImageStat.Stat(im).mean
    return r < 100 and r > g * 1.3 and r > b * 1.5   # Home ~(63, 40, 32); the map ~(116, 117, 119)


def to_row(s, mode="trial", category="city", boot="auto"):
    """Boot to an Arcade course row: mode trial (Time Trial) or race (Single Race); category world, original, city
    or dirt (the 2 x 2 category screen, World selected first). boot: where the save lands after Start, 'home'
    (r5_saves: GT Home) or 'map' (saves-ss-r5: the GT map)."""
    s.queue("vwait 1400")
    s.press("start", 600)
    first = s.shot("r_home")
    if boot == "auto":
        boot = "home" if is_home(first) else "map"
        log(f"the save starts at GT {boot}")
    if boot == "home":
        s.press("triangle", 1000)
    s.press("triangle", 500, "cross", 1800); s.shot("r_title")
    s.press("start", 200, "dpdown", 40, "dpup", 40, "cross", 150); s.shot("r_modes")
    if mode == "trial":
        s.press("dpright", 40)
    s.press("cross", 600); s.shot("r_cats")
    moves = {"world": [], "original": ["dpright"], "city": ["dpdown"], "dirt": ["dpright", "dpdown"]}[category]
    for m in moves:
        s.press(m, 40)
    s.press("cross", 600)
    return s.shot("r_row")


def to_grid_ss(s, mode="trial", course="ssr5"):
    """Boot to the grid of a City course (default Special Stage Route 5 at night), on saves-ss-r5 (its City row has
    every course)."""
    last = to_row(s, mode, "city")
    target = CITY_ORDER.index(course)
    if os.environ.get("LANEF_SETTINGS"):
        # (the label crops are 4:3; with a settings file, e.g. 16:9, step blindly from the row's first entry, Amalfi)
        for _ in range(target):
            s.queue("button dpright 1", "wait 1", "button dpright 0", "vwait 60")
        s.shot("k_blind")
        log(f"course row: {target} steps right, blind ({course})")
        s.press("cross", 600, "cross", 600, "cross", 300, "cross", 300, "cross", 300, "cross", 600, "cross", 900, "cross", 900)
        s.shot("r_grid")
        log("on the grid")
        return
    for i in range(1, 41):
        cur = which_course(last, HERE / "info_city")
        log(f"course row: {cur}")
        if cur == course:
            break
        if cur in CITY_ORDER:
            b = "dpright" if CITY_ORDER.index(cur) < target else "dpleft"
            s.queue(f"button {b} 1", "wait 1", f"button {b} 0", "vwait 60")
        else:
            s.queue("vwait 30")
        last = s.shot(f"k{i}")
    else:
        raise SystemExit(f"{course} not selected")
    s.press("cross", 600, "cross", 600, "cross", 300, "cross", 300, "cross", 300, "cross", 600, "cross", 900, "cross", 900)
    s.shot("r_grid")
    log("on the grid")


def label_diff(path, ref_name):
    """Mean difference of the bottom label line against a reference (lanef_tools/<ref_name>.png)."""
    # both shrunk to 220 x 8 first: a session at 320 x 180 shows the text blurred, the references are sharp
    small = (220, 8)
    shot = read_ppm(path).resize((1280, 720)).crop((200, 672, 1080, 705)).convert("L").resize(small, Image.LANCZOS)
    ref = Image.open(HERE / (ref_name + ".png")).resize(small, Image.LANCZOS)
    return ImageStat.Stat(ImageChops.difference(shot, ref)).mean[0]


def to_race_bspec(s, event="na4", spec="b"):
    """Boot to a B-spec race on Special Stage Route 5 (Reverse) at night: GT mode, Professional Events, Race of NA
    Sports, race 4, on saves-ss-r5 (its BMW M3 CSL is NA; the event's races are all won, so GT4 asks to clear
    the records: Don't Clear). Ends in the race with the view changed once (a road-level view).
    event 'club4': Clubman Cup race 4 instead (Clubman Stage Route 5, 5 laps; the hall's first event, races all won)."""
    if event == "sunday4":   # Beginner Events > Sunday Cup race 4: C.S. Route 5 Reverse (lane F's r11s2 session)
        s.queue("vwait 1400")
        s.press("start", 600)
        if is_home(s.shot("r_home")):
            s.press("triangle", 600)
        for m in ("dpdown", "dpright"):                        # the map pointer to Beginner Events
            s.queue(f"button {m} 1", "vwait 4", f"button {m} 0", "vwait 60")
        s.shot("r_map_beginner")
        s.press("cross", 500, "cross", 500, "cross", 600)      # the hall (Sunday Cup first), regulations OK, the event page
        s.press("dpright", 60, "dpright", 60, "dpright", 60)   # race 4: Clubman Stage Route 5 Reverse
        s.shot("r_sunday_race4")
        s.press("cross", 500, "cross", 500, "cross", 700)      # Entry, 'clear the records?', Don't Clear
        s.shot("r_race_screen")
        s.press("dpright", 60, "cross", 1500)                  # B-spec, the start sequence
        s.press("cross", 120)
        s.shot("r_bspec_a")
        log("B-spec race running (Sunday Cup race 4)")
        return
    if event == "club4":
        s.queue("vwait 1400")
        s.press("start", 600)
        if is_home(s.shot("r_home")):
            s.press("triangle", 600)
        for m in ("dpright", "dpright", "dpright", "dpdown"):
            s.queue(f"button {m} 1", "vwait 4", f"button {m} 0", "vwait 60")
        d = label_diff(s.shot("r_map_pro"), "lbl_professional")
        log(f"map: Professional Events label differs by {d:.1f}")
        if d > 15:
            raise SystemExit("the map pointer is not on Professional Events")
        s.press("cross", 500)                                  # the hall, Clubman Cup first
        s.press("cross", 500, "cross", 600)                    # regulations (OK), the event page (race 1)
        s.press("dpright", 60, "dpright", 60, "dpright", 60)   # race 4: Clubman Stage Route 5 (checked by eye: clubprobe)
        s.shot("r_club_race4")
        s.press("cross", 900, "cross", 900, "cross", 900)      # Entry, 'clear the records?', Don't Clear
        s.shot("r_race_screen")
        s.press("dpright", 60, "cross", 1500)                  # B-spec, the start sequence
        s.press("cross", 120)
        s.shot("r_bspec_a")
        log("B-spec race running (Clubman Cup race 4)")
        return
    s.queue("vwait 1400")
    s.press("start", 600)
    if is_home(s.shot("r_home")):
        s.press("triangle", 600)
    for m in ("dpright", "dpright", "dpright", "dpdown"):      # Home > Extreme > Korean Showroom > Japanese > Professional
        s.queue(f"button {m} 1", "vwait 4", f"button {m} 0", "vwait 60")
    d = label_diff(s.shot("r_map_pro"), "lbl_professional")
    log(f"map: Professional Events label differs by {d:.1f}")
    if d > 15 and not os.environ.get("LANEF_SETTINGS"):   # (4:3 references: log-only with a settings file, e.g. 16:9)
        raise SystemExit("the map pointer is not on Professional Events")
    s.press("cross", 500)                                      # the hall, Clubman Cup first
    s.press("dpdown", 120, "dpdown", 120)                      # Tuning Car GP, Race of NA Sports
    s.press("cross", 500, "cross", 600)                        # regulations (OK is the default), the event page
    s.press("dpright", 40, "dpright", 40, "dpright", 60)       # race 4
    d = label_diff(s.shot("r_na_race4"), "lbl_na_race4")
    log(f"event: race 4 label differs by {d:.1f}")
    if d > 15 and not os.environ.get("LANEF_SETTINGS"):   # (4:3 references: log-only with a settings file, e.g. 16:9)
        raise SystemExit("not on Race of NA Sports race 4 (Special Stage Route 5)")
    s.press("cross", 900)                                      # the race page: Entry
    s.press("cross", 900)                                      # Entry: 'clear the records?' dialog
    s.press("cross", 900)                                      # Don't Clear (the default): the race screen
    s.shot("r_race_screen")
    if spec == "a":   # A-spec (the default entry): the start sequence, then the race with the player driving
        s.press("cross", 1500)
        s.shot("r_aspec_a")
        log("A-spec race running")
        return
    s.press("dpright", 60, "cross", 1500)                      # B-spec: the start sequence, then the race (TV view)
    s.press("cross", 120)                                      # Change View once, in the race: a road-level view
    s.shot("r_bspec_a")
    s.queue("vwait 120")
    s.shot("r_bspec_b")                                        # 2 s later: the same kind of view (checked by eye)
    log("B-spec race running")


def to_grid(s):
    """Boot to the grid of Clubman Stage Route 5 at night (docs/modern/CAPTURES.md, N0 route)."""
    last = to_row(s, "trial", "city")
    for i in range(1, 41):
        cur = which_course(last)
        log(f"course row: {cur}")
        if cur == "csr5":
            break
        b = {"citta": "dpright", "newyork": "dpright", "tokyo": "dpright", "seattle": "dpleft", "seoul": "dpleft"}.get(cur)
        if b:
            s.queue(f"button {b} 1", "wait 1", f"button {b} 0", "vwait 60")
        else:
            s.queue("vwait 30")
        last = s.shot(f"k{i}")
    else:
        raise SystemExit("C.S.Route 5 not selected")
    s.press("cross", 600, "cross", 600, "cross", 300, "cross", 300, "cross", 300, "cross", 600, "cross", 900, "cross", 900)
    s.shot("r_grid")
    log("on the grid")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("job", choices=["scan", "record", "hold", "gridhold", "gridprobe", "boot", "mapprobe", "clubprobe", "hallprobe", "viewprobe"])
    ap.add_argument("--moves", default="", help="mapprobe: comma-separated d-pad taps from Home on the GT map")
    ap.add_argument("--saves-dir", help="a save folder used in place (default: a fresh copy of build/r5_saves)")
    ap.add_argument("--course", default="csr5", choices=["ssr5b", "csr5b", "csr5rb"] + CITY_ORDER,
                    help="ssr5 needs --saves-dir pointing at a copy of build/saves-ss-r5")
    ap.add_argument("--throttle", default="cross", choices=["cross", "r2"],
                    help="the accelerator in the save's controller setup (saves-ss-r5: R2; Cross brakes)")
    ap.add_argument("--mode", default="trial", choices=["trial", "race"])
    ap.add_argument("--category", default="city", choices=["world", "original", "city", "dirt"])
    ap.add_argument("--viewbutton", default="select", help="viewprobe: the button that changes the view")
    ap.add_argument("--hall", default="professional", choices=["beginner", "professional"])
    ap.add_argument("name")
    ap.add_argument("--size", default="640x360")
    ap.add_argument("--seconds", type=int, default=60)
    ap.add_argument("--cap")
    ap.add_argument("--count", type=int, default=600)
    ap.add_argument("--after", type=int, default=0, help="record: VBlanks of driving before the recording starts")
    ap.add_argument("--bursts", action="store_true", help="record: also capture every live frame while recording")
    ap.add_argument("--tail", type=int, default=0,
                    help="record: VBlanks to run after the start point (default --count; from boot, the rest)")
    ap.add_argument("--shots", type=int, default=0, help="record: a live shot every N VBlanks while recording")
    a = ap.parse_args()
    locks = take_slot(f"live game session: N0 {a.job} {a.name}", live=True)
    s = None
    try:
        # a recording armed on the trigger file <session>/rec_go: the record job writes it itself; for boot (an
        # interactive session driven with q.py) --cap arms it and the operator writes rec_go when it should start
        s = Session(a.name, a.size, (a.cap, a.count) if (a.job == "record" or a.cap) else None, a.saves_dir)
        if a.job == "mapprobe":
            # from GT Home to the map, then one short tap per move, the bottom label saved after each
            s.queue("vwait 1400")
            s.press("start", 600)
            first = s.shot("r_home")
            if is_home(first):
                s.press("triangle", 600)
            labels = [read_ppm(s.shot("mp00")).resize((1280, 720)).crop((160, 660, 1120, 712))]
            for i, m in enumerate([m for m in a.moves.split(",") if m], 1):
                s.queue(f"button {m} 1", "vwait 4", f"button {m} 0", "vwait 60")
                labels.append(read_ppm(s.shot(f"mp{i:02d}")).resize((1280, 720)).crop((160, 660, 1120, 712)))
            sheet = Image.new("RGB", (960, 52 * len(labels)))
            for i, im in enumerate(labels):
                sheet.paste(im, (0, 52 * i))
            sheet.save(s.dir / "mapprobe.png")
            log(f"mapprobe: {len(labels)} labels in {s.dir / 'mapprobe.png'}")
            return
        if a.job == "boot":
            # boot, Start, then hand over to q.py until the file 'release' appears
            s.queue("vwait 1400")
            s.press("start", 600)
            s.shot("b_first")
            log("holding after boot; touch release to quit")
            while not (s.dir / "release").exists():
                if s.proc.poll() is not None:
                    break
                time.sleep(2)
            return
        if a.job == "viewprobe":
            # an Arcade single race at night on --course (cars on the grid), throttle held: two shots a view, then the
            # view button (--viewbutton, Select by default) and again, --count views (the owner's night bands, step 0)
            if a.course == "ssr5b":   # GT mode: Race of NA Sports race 4 (S.S. Route 5 Reverse, night) in A-spec
                to_race_bspec(s, "na4", spec="a")
            else:
                to_grid_ss(s, "race", a.course)
            go = "axis righttrigger 32767" if a.throttle == "r2" else "button cross 1"
            s.queue(go, "vwait 240")
            for v in range(a.count):
                s.shot(f"view{v}_a")
                s.queue("vwait 45")
                s.shot(f"view{v}_b")
                s.queue(f"button {a.viewbutton} 1", "vwait 6", f"button {a.viewbutton} 0", "vwait 30")
            log(f"viewprobe {a.course}: {a.count} views")
            return
        if a.job == "hallprobe":
            # GT mode, one hall (--category beginner | professional): each event's page (its five course logos), up to
            # --count events: hall -> event (Cross), regulations OK (Cross), the page; Triangle back to the hall; Down
            s.queue("vwait 1400")
            s.press("start", 600)
            if is_home(s.shot("h_home")):
                s.press("triangle", 600)
            moves = ("dpdown", "dpright") if a.hall == "beginner" else ("dpright", "dpright", "dpright", "dpdown")
            for m in moves:
                s.queue(f"button {m} 1", "vwait 4", f"button {m} 0", "vwait 60")
            s.shot("h_map")
            s.press("cross", 500)
            s.shot("h_hall")
            for e in range(a.count):
                s.press("cross", 500, "cross", 600)
                s.shot(f"h_event{e + 1}")
                s.press("triangle", 600)
                s.shot(f"h_back{e + 1}")
                s.press("dpdown", 120)
            log(f"hallprobe {a.hall}: {a.count} events")
            return
        if a.job == "clubprobe":
            # GT mode, Professional Events, Clubman Cup (the hall's first event): a shot of the event page per race
            s.queue("vwait 1400")
            s.press("start", 600)
            if is_home(s.shot("c_home")):
                s.press("triangle", 600)
            for m in ("dpright", "dpright", "dpright", "dpdown"):
                s.queue(f"button {m} 1", "vwait 4", f"button {m} 0", "vwait 60")
            d = label_diff(s.shot("c_map_pro"), "lbl_professional")
            log(f"map: Professional Events label differs by {d:.1f}")
            if d > 15:
                raise SystemExit("the map pointer is not on Professional Events")
            s.press("cross", 500)                    # the hall, Clubman Cup first
            s.shot("c_hall")
            s.press("cross", 500, "cross", 600)      # regulations (OK), the event page
            s.shot("c_race1")
            for k in range(2, 7):
                s.press("dpright", 60)
                s.shot(f"c_race{k}")
            log("clubprobe: shots c_race1..6")
            return
        if a.job == "gridhold":
            # to the grid of S.S. Route 5 (Arcade, Normal layout by default), then hand over to q.py
            to_grid_ss(s, a.mode)
            log("holding on the grid; touch release to quit")
            while not (s.dir / "release").exists():
                if s.proc.poll() is not None:
                    break
                time.sleep(2)
            return
        if a.job == "gridprobe":
            # to the grid of --course (a City course, on saves-ss-r5), drive 5 s, a shot: is it night there?
            to_grid_ss(s, a.mode, a.course)
            go, stop = (("axis righttrigger 32767", "axis righttrigger -32768") if a.throttle == "r2"
                        else ("button cross 1", "button cross 0"))
            s.queue(go, "vwait 300")
            s.shot("probe_drive")
            s.queue(stop)
            log(f"gridprobe {a.course}: {s.dir / 'probe_drive.ppm'}")
            return
        if a.job == "hold":
            # to a course row, then hand over: commands come from q.py until the file 'release' appears
            to_row(s, a.mode, a.category)
            log("holding at the course row; touch release to quit")
            while not (s.dir / "release").exists():
                if s.proc.poll() is not None:
                    break
                time.sleep(2)
            return
        if a.course == "ssr5b":
            to_race_bspec(s)
        elif a.course == "csr5b":
            to_race_bspec(s, "club4")
        elif a.course == "csr5rb":
            to_race_bspec(s, "sunday4")
        elif a.course == "ssr5":
            to_grid_ss(s, a.mode)
        elif a.course != "csr5" or a.saves_dir:
            to_grid_ss(s, a.mode, a.course)   # any City course on saves-ss-r5 (its row has them all: info_city's crops)
        else:
            to_grid(s)
        go, stop = (("axis righttrigger 32767", "axis righttrigger -32768") if a.throttle == "r2"
                    else ("button cross 1", "button cross 0"))
        if a.course in ("ssr5b", "csr5b", "csr5rb"):
            go, stop = "vwait 1", "vwait 1"      # B-spec: the game drives; no pedal
        if a.job == "scan":
            cmds = [go]
            bursts = (a.seconds * 60 + 599) // 600
            for k in range(1, bursts + 1):
                cmds += [f"capture s{k} 600", "vwait 600"]
            cmds += [stop, "vwait 900"]   # a burst can run past its VBlanks when the host falls behind the game
            s.queue(*cmds)
            s.shot("done", timeout=a.seconds * 3 + 600)
        else:
            s.queue(go, f"vwait {max(1, a.after)}")
            s.shot("rec_start", timeout=a.after + 600)
            (s.dir / "rec_go").write_text("go\n")
            log("recording")
            if a.bursts:
                # the live frames of the same VBlanks, every frame, to compare with a replay of this capture
                cmds = []
                for k in range(1, (a.count + 599) // 600 + 1):
                    cmds += [f"capture L{k} 600", "vwait 600"]
                s.queue(*cmds, "vwait 900")
            if a.shots:
                # a live shot every --shots VBlanks while recording (live against replay at the same VBlanks)
                cmds = []
                for k in range((a.tail or a.count) // a.shots):
                    cmds += [f"capture live{k * a.shots:05d}", f"vwait {a.shots}"]
                s.queue(*cmds)
            wait = (a.tail or a.count) + 60
            s.queue(f"vwait {wait}")
            s.shot("rec_end", timeout=wait * 3 + 1200)
            s.queue(stop)
        log("done")
    finally:
        if s:
            s.quit()
        for lock in locks:
            shutil.rmtree(lock, ignore_errors=True)
        log("locks released")


if __name__ == "__main__":
    main()
