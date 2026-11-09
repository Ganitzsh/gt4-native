#!/usr/bin/env python3
"""The Odin device run (docs/modern/PLAN.md C6b): one command on the desktop or the Mac runs the build items on the
owner's Odin over ssh and brings the report back.

    py tools/device_run.py --ssh HOST [--remote DIR] [--package DIR] [--items a,b,...] [--out DIR]

HOST is an ssh destination (an alias from ~/.ssh/config, or user@address); DIR on the device holds the copied REF
captures, the reference hashes and the save folders (default ~/gt4-device); --package is the Odin build's folder
(default ~/Games/GT4-Native-linux). The script copies itself and the tools it needs to DIR/tools, runs its device half
there (`--device`), and copies the report folder back to --out (default build/device_run/<time>). The report holds
numbers, logs and profiles only: frames of the game are hashed or measured on the device and deleted there, and the
save copies a run makes are deleted after it (the originals in DIR are never written).

The device half checks Python first (3.8 or newer), then the data's files and sizes, then free space (it refuses to
start under --min-free-gb, default 3: its working peak is about 1.5 GB), then runs each item and writes report.md
and report.json. An item that fails or can't run is reported as such; the others still run; one whose capture isn't
in the data folder yet is 'missing' and doesn't fail the run. The items:

    env             the device: kernel, OS, Vulkan driver (vulkaninfo), FEX, the package's build
    provoking       VK_EXT_provoking_vertex and VK_EXT_shader_stencil_export on the device (vulkaninfo; B7, C6, C6b)
    c6_race7        race7.cap replayed at 1280x960, original: every frame's SHA-256 against the x64 hashes (C6)
    c6_orig         C6's ORIG by PLAN section 2's per-device-class rule: race7, race_grid43 and race_pe2_prog
                    replayed with the hash lists' flags, every frame hashed against its list (the reference class,
                    x64: identical); where DIR/ref/<capture>/ holds the reference's frames (--make-refs), each is
                    compared on pixel values by tools/orig_compare.py (section 2's interim rule: no pixel over 32
                    levels, no 32-GS-pixel steps, the over-2 and 1-2 shares against the capture's baselines for the
                    class once set), else the first --keep-differing N
                    differing frames are kept as PNG for the desktop's comparison (the routine run's sample)
    b6_span         B6's scene-cache span on race_boot.cap after its first 300 frames, per frame, p95 (B6c)
    night           the same span on the night lap with lighting.night = enhanced against original (F2b)
    hybrid          render.scene = hybrid against original on race_grid43.cap, every 10th frame: PARITY's two
                    shares, and the front-end and render threads' CPU from --profile-json (B8)
    pipeline_cache  race7.cap twice with GT4_PIPELINE_CACHE in the work folder, emptied first: the cold run's frame
                    times against the warm one's (B2)
    mixer           audio.mixer = host, audio.interpolation = sinc, at audio.spatial original, stereo and hrtf: the
                    host summary's mix time (and hrtf's buses) on a replay of race_mixer_host.cap (lane D's REF capture
                    recorded with the host's mixer; D5, D9); underruns are a device item
    load_times      GT4_LOAD_STATS=1 on the route to PE race 2 (docs/modern/routes/race_gt_pe2.txt) with a copy of
                    saves-pe-race2: each load's blocked VBlanks and time; the race load against 0.2 s (D10 (c))
    photo_travel    tools/photo_check.py with a copy of r5_saves: the shot's reads and time (lane A's CT24 fix)
    night_bands     lap_ssr5r and lap_csr5 at the owner's settings at 1280x800, every 100th frame from 600 through
                    tools/band_measure.py (lane F's), the frames deleted: per frame a triage score, to compare with the
                    desktop's same frames (the live dump on L3 + R3 is the item's other half)

Items that wait for their ticket (B16c's memory and GPU cost, F6-F9's budgets, the settings pages' cost, the
shooting screen with the held picture) are listed as pending until they are added here. The device items of C6b
(R4's steps, latency, the mixer's underruns, the texture pack, the pads) need a person and are not run.
"""
import sys

if sys.version_info < (3, 8):
    sys.exit(f"device_run: Python 3.8 or newer is needed, this is {sys.version.split()[0]}")

import argparse
import datetime
import glob
import hashlib
import json
import os
import platform
import re
import shutil
import statistics
import subprocess
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = ("device_run.py", "photo_check.py", "perf_report.py", "band_measure.py", "orig_compare.py")
ROUTE = "docs/modern/routes/race_gt_pe2.txt"

# The files the device half reads in DIR (C6b; the coordinator copies them), with their sizes in bytes.
DATA = {
    "race7.cap": 119256448,
    "race_grid43.cap": 320675952,
    "race_boot.cap": 2252306688,
    "lap_csr5.cap": 3888681808,
    "lap_ssr5r.cap": 4061126560,
    "race_mixer_host.cap": 3144205376,
    "race_pe2_prog.cap": 2313051136,
    "orig_r6_race7.sha256": 7949,
    "orig_r6_race_grid43.sha256": 55355,
}
SAVES = ("saves-pe-race2", "r5_saves")
# The mixer row's capture: a race recorded with audio.mixer = host (lane D's REF capture, CAPTURES.md).
MIXER_CAP = "race_mixer_host.cap"
MIXER_FRAMES = 8121   # its VBlanks: a replay doesn't end by itself at the capture's end, so --frames is its length
# The night bands' laps (lane F's, CAPTURES' night section): every 100th frame from 600 (after the re-upload window).
NIGHT_LAPS = (("lap_ssr5r.cap", 10200), ("lap_csr5.cap", 9000))
# C6's ORIG on the device (c6_orig): each REF capture, its VBlanks, the hash list its frames are compared with (lane
# B's R6 lists, the re-baseline 7f58836 and later; for the progressive race, which has none, the desktop's, made with
# the same flags by `--make-hashes`), and the step of its reference frames in DIR/ref/ (`--make-refs`: every frame of
# race7, every 10th of the others; the coordinator's choice, 2026-11-09).
ORIG_CAPS = (("race7.cap", 100, "orig_r6_race7.sha256", 1),
             ("race_grid43.cap", 700, "orig_r6_race_grid43.sha256", 10),
             ("race_pe2_prog.cap", 4915, "orig_desktop_race_pe2_prog.sha256", 10))
REF_DIR = "ref"
# The reference device class (PLAN section 2's ORIG row): x64, the desktop's; there ORIG is bit-identical.
REFERENCE_MACHINES = ("x86_64", "amd64")
# Each class's baselines per capture (section 2's interim rule, until GS-exact texel sampling): the 1-2 level share
# and the over-2 share ({"1-2": %, "over_2": %, the highest frame's}), set by the class's every-frame run (a frame
# fails 5 points over the first or orig_compare.OVER_FACTOR times the second). Until a capture has them, its shares
# are reported and only the over-32 cap and the grid test judge it. aarch64: the Odin's Adreno 830 under Turnip (lane
# B's first frames, 2026-11-09: race7 1-2 at 23.2-23.9 %, over 2 at 0.02-0.03 %; race_pe2_prog over 2 at 0.10-0.49 %).
ORIG_BASELINES = {"aarch64": {}}
# the flags the hash lists were made with (CAPTURES' re-baselines): any other flag changes every hash
ORIG_FLAGS = ["--size", "1280x960", "--preset", "original", "--set", "texture.hd_pack=off", "--set",
              "display.frame_interpolation=off", "--set", "debug.overlay=off"]
ITEMS = ("env", "provoking", "c6_orig", "c6_race7", "b6_span", "night", "hybrid", "pipeline_cache", "mixer",
         "load_times", "photo_travel", "night_bands")
PENDING = {
    "b16c_memory": "B16c (lane B): its kept images' memory, with the freed MSAA targets beside it",
    "b16c_gpu": "B16c (lane B): FSR 2 on the game's frames against the GS scene's own resolve",
    "lights": "F6-F9 (lane F): each setting's GPU time at the Odin's device preset",
    "pages": "M3 (lane M): the page layer's draw cost with the hub open, if lane M asks",
    "shooting_screen": "P2c's follow-up (lane B): the shooting screen's rate with the held picture",
}
FIXED = ["--set", "display.frame_interpolation=off", "--set", "texture.hd_pack=off"]
# The desktop's numbers beside the Odin's, from the same build (lane C's desktop run of the device half on main's
# Windows host): set them again when the package moves on.
DESKTOP = {"commit": "97d0809a", "b6_p95_us": 348,
           # (the front end's wall clock: +342 and +28 us in two runs of one build under other lanes' load)
           "night_added_p95_us": "28 to 342"}


# ---------------------------------------------------------------------------------------------------------- desktop

def desktop(a):
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    out = a.out or os.path.join(ROOT, "build", "device_run", stamp)
    remote_tools = f"{a.remote}/tools"
    remote_report = f"{a.remote}/reports/{stamp}"
    ssh = ["ssh", "-o", "BatchMode=yes", a.ssh]

    def run(cmd, **kw):
        print("+", " ".join(cmd), flush=True)
        return subprocess.run(cmd, **kw)

    if run(ssh + [f"mkdir -p {remote_tools}/tools {remote_tools}/docs/modern/routes {remote_report}"]).returncode:
        sys.exit("device_run: ssh failed (is the Odin awake, and the host in ~/.ssh/config?)")
    files = [os.path.join(ROOT, "tools", t) for t in TOOLS]
    if run(["scp", "-q", "-o", "BatchMode=yes"] + files + [f"{a.ssh}:{remote_tools}/tools/"]).returncode or \
            run(["scp", "-q", "-o", "BatchMode=yes", os.path.join(ROOT, ROUTE),
                 f"{a.ssh}:{remote_tools}/{os.path.dirname(ROUTE)}/"]).returncode:
        sys.exit("device_run: copying the tools to the device failed")
    device_cmd = (f"python3 {remote_tools}/tools/device_run.py --device --data {a.remote} --package {a.package} "
                  f"--report {remote_report} --min-free-gb {a.min_free_gb}")
    if a.items:
        device_cmd += f" --items {a.items}"
    if a.keep_differing:
        device_cmd += f" --keep-differing {a.keep_differing}"
    code = run(ssh + [device_cmd]).returncode
    os.makedirs(out, exist_ok=True)
    if run(["scp", "-q", "-r", "-o", "BatchMode=yes", f"{a.ssh}:{remote_report}/.", out]).returncode:
        sys.exit(f"device_run: the device half exited {code}, and copying the report back failed ({remote_report})")
    print(f"device_run: the device half exited {code}; the report is in {out} (report.md)")
    sys.exit(code)


# ----------------------------------------------------------------------------------------------------------- device

class Device:
    def __init__(self, a):
        self.a = a
        self.data = os.path.abspath(os.path.expanduser(a.data))
        self.package = os.path.abspath(os.path.expanduser(a.package))
        self.report = os.path.abspath(os.path.expanduser(a.report))
        self.work = os.path.join(self.report, "work")
        os.makedirs(self.work, exist_ok=True)
        os.makedirs(os.path.join(self.report, "logs"), exist_ok=True)
        exe = [p for p in (os.path.join(self.package, "gt4_host"), os.path.join(self.package, "gt4_host.exe"))
               if os.path.isfile(p)]
        self.host = exe[0] if exe else None
        launcher = os.path.join(self.package, "gt4-native.sh")
        self.launcher = launcher if os.path.isfile(launcher) else self.host   # (live game runs: Proton's Wine)
        self.results = {}

    # helpers
    def log(self, name, text):
        with open(os.path.join(self.report, "logs", name), "w", encoding="utf-8", errors="replace") as f:
            f.write(text)

    def cap(self, name):
        return os.path.join(self.data, name)

    def replay(self, cap, frames, extra, name, env=None, capture=None, timeout=3600):
        """gt4_host replaying a capture headless at 1280x960; returns (exit code, output)."""
        cmd = [self.host, "--headless", "--size", "1280x960", "--replay", self.cap(cap), "--frames", str(frames)]
        if capture:
            cmd += ["--capture-frames", capture[0], "--capture-dir", capture[1]]
        cmd += extra + FIXED
        e = dict(os.environ, **(env or {}))
        t = time.time()
        try:
            r = subprocess.run(cmd, capture_output=True, text=True, env=e, cwd=self.package, timeout=timeout,
                               errors="replace")
            out, code = r.stdout + r.stderr, r.returncode
        except subprocess.TimeoutExpired as x:
            out, code = (x.stdout or "") + f"\n(timed out after {timeout} s)", -1
        self.log(name + ".log", f"$ {' '.join(cmd)}\n({time.time() - t:.0f} s, exit {code})\n" + out)
        return code, out

    @staticmethod
    def profile(path, skip=0):
        """Per-frame records of a --profile-json file: [(frame dict)], the first `skip` left out."""
        frames = []
        if os.path.isfile(path):
            for line in open(path, encoding="utf-8", errors="replace"):
                try:
                    frames.append(json.loads(line))
                except ValueError:
                    pass
        return frames[skip:]

    @staticmethod
    def stage(frames, group, name, field):
        """A stage's per-frame values (same-named entries in a frame added up)."""
        values = []
        for f in frames:
            total = None
            for s in f.get(group, []) or []:
                if s.get("name") == name and field in s:
                    total = (total or 0.0) + float(s[field])
            if total is not None:
                values.append(total)
        return values

    @staticmethod
    def pct(values, q):
        if not values:
            return None
        v = sorted(values)
        return v[int(q / 100.0 * (len(v) - 1))]   # (by index, floored: lane B's b6_report.py)

    def save_copy(self, name):
        src, dst = os.path.join(self.data, name), os.path.join(self.work, "saves-" + name)
        shutil.rmtree(dst, ignore_errors=True)
        shutil.copytree(src, dst)
        return dst

    # preflight
    def preflight(self, items):
        needs = {"c6_race7": ("race7.cap", "orig_r6_race7.sha256"), "b6_span": ("race_boot.cap",),
                 "night": ("lap_csr5.cap",), "hybrid": ("race_grid43.cap",), "pipeline_cache": ("race7.cap",),
                 "load_times": ("saves-pe-race2",), "photo_travel": ("r5_saves",),
                 "mixer": (MIXER_CAP,), "night_bands": tuple(cap for cap, _ in NIGHT_LAPS),
                 # (race_pe2_prog and its desktop list are optional: the item says 'missing' for them)
                 "c6_orig": ("race7.cap", "orig_r6_race7.sha256", "race_grid43.cap", "orig_r6_race_grid43.sha256")}
        wanted = {f for i in items for f in needs.get(i, ())}
        problems = []
        for name, size in DATA.items():
            if name not in wanted:
                continue
            p = self.cap(name)
            if not os.path.isfile(p):
                problems.append(f"missing {p}")
            elif os.path.getsize(p) != size:
                problems.append(f"{p} is {os.path.getsize(p)} bytes, expected {size}")
        for name in SAVES:
            if name in wanted and not os.path.isdir(os.path.join(self.data, name)):
                problems.append(f"missing the save folder {os.path.join(self.data, name)}")
        if not self.host:
            problems.append(f"no gt4_host in {self.package}")
        free = shutil.disk_usage(self.report).free / 1e9
        if free < self.a.min_free_gb:
            problems.append(f"{free:.1f} GB free where the report goes, under the {self.a.min_free_gb} GB it needs")
        return problems, free

    # items
    def item_env(self):
        def cmd(c):
            try:
                return subprocess.run(c, capture_output=True, text=True, timeout=60, errors="replace").stdout.strip()
            except (OSError, subprocess.TimeoutExpired):
                return ""
        info = {"python": sys.version.split()[0], "machine": platform.machine(), "kernel": platform.release()}
        if os.path.isfile("/etc/os-release"):
            m = re.search(r'^PRETTY_NAME="?([^"\n]*)', open("/etc/os-release").read(), re.M)
            info["os"] = m.group(1) if m else ""
        summary = cmd(["vulkaninfo", "--summary"])
        self.log("vulkaninfo_summary.txt", summary)
        for key in ("deviceName", "driverName", "driverInfo", "apiVersion"):
            m = re.search(rf"{key}\s*=\s*(.+)", summary)
            if m:
                info[key] = m.group(1).strip()
        for c in (["FEXInterpreter", "--version"], ["FEX", "--version"]):
            v = cmd(c)
            if v:
                info["fex"] = v.splitlines()[0]
                break
        builds = [p for p in glob.glob(os.path.join(self.package, "*.txt")) if "build" in os.path.basename(p).lower()]
        info["package"] = self.package
        if builds:
            info["package_build"] = open(builds[0], encoding="utf-8", errors="replace").read()[:400]
        return {"status": "reported", "numbers": info}

    def item_provoking(self):
        try:
            full = subprocess.run(["vulkaninfo"], capture_output=True, text=True, timeout=120, errors="replace").stdout
        except (OSError, subprocess.TimeoutExpired):
            return {"status": "error", "note": "vulkaninfo did not run"}
        present = "VK_EXT_provoking_vertex" in full
        m = re.search(r"provokingVertexLast\s*=\s*(\w+)", full)
        # The night bands (PLAN C6b): without the stencil export, every draw writing alpha marks DATE's stencil mirror
        # stale and the next DATE draw rebuilds it (gsn_scene.cpp); the host enables the export when the device has it.
        return {"status": "reported", "numbers": {"VK_EXT_provoking_vertex": present,
                                                    "provokingVertexLast": m.group(1) if m else None,
                                                    "VK_EXT_shader_stencil_export":
                                                        "VK_EXT_shader_stencil_export" in full},
                "note": "absent: a B7 finding, not a C6 failure; no stencil export: DATE's rebuild path (C6b)"}

    @staticmethod
    def write_png(path, w, h, rgb, up=False):
        """An 8-bit RGB PNG in plain Python (zlib): a kept frame (no filter), or with `up` a reference frame (each row
        less the one above, PNG's Up filter: smaller; band_measure.py's reader takes it)."""
        import struct
        import zlib

        def chunk(kind, data):
            return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
        stride = w * 3
        if up:
            parts = [b"\x02" + rgb[0:stride]]
            for y in range(1, h):
                line, above = rgb[y * stride:(y + 1) * stride], rgb[(y - 1) * stride:y * stride]
                parts.append(b"\x02" + bytes((x - u) & 255 for x, u in zip(line, above)))
            rows = b"".join(parts)
        else:
            rows = b"".join(b"\x00" + rgb[y * stride:(y + 1) * stride] for y in range(h))
        with open(path, "wb") as f:
            f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
                    chunk(b"IDAT", zlib.compress(rows, 6)) + chunk(b"IEND", b""))

    def streamed_hashes(self, cap, frames, name, on_frame, capture_list=None):
        """Replays `cap` with ORIG_FLAGS, every frame captured, and hands each frame file to on_frame(number, path)
        once it is whole, then deletes it: the disk holds a frame or two, not the whole replay (race_grid43 every
        frame is 2.6 GB at 1280x960). Whole: a later frame exists and the file's size is its P6 header's length
        plus width x height x 3 (should the host's writes ever overlap, a part-written frame would otherwise hash
        as a differing one); a frame still short when the replay has ended is left out (reported as missing, not
        as differing). The frames are game data: whatever happens (on_frame raising, a full disk), the replay is
        stopped and the folder removed."""
        d = os.path.join(self.work, "orig_" + name)
        shutil.rmtree(d, ignore_errors=True)
        os.makedirs(d)
        cmd = [self.host, "--headless", "--replay", self.cap(cap), "--frames", str(frames), "--capture-frames",
               capture_list or f"1-{frames}", "--capture-dir", d] + ORIG_FLAGS
        log = open(os.path.join(self.report, "logs", f"c6_orig_{name}.log"), "w", encoding="utf-8", errors="replace")
        log.write("$ " + " ".join(cmd) + "\n")
        log.flush()
        proc = None

        def whole(path):
            try:
                with open(path, "rb") as f:
                    head = f.read(64)
                m = re.match(rb"P6\s+(\d+)\s+(\d+)\s+255\s", head)
                return bool(m) and os.path.getsize(path) == m.end() + int(m.group(1)) * int(m.group(2)) * 3
            except OSError:
                return False

        def ready(final):
            files = sorted(f for f in os.listdir(d) if f.endswith(".ppm"))
            for f in files if final else files[:-1]:
                m = re.search(r"(\d+)\.ppm$", f)
                path = os.path.join(d, f)
                if not whole(path):
                    if not final:
                        continue   # (still being written: the next poll takes it)
                    log.write(f"device_run: {f} short or unreadable at the end: left out (missing)\n")
                elif m:
                    on_frame(int(m.group(1)), path)
                os.remove(path)
        try:
            proc = subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT, cwd=self.package)
            while proc.poll() is None:
                ready(False)
                time.sleep(0.2)
            ready(True)
            log.write(f"\nexit {proc.returncode}\n")
            return proc.returncode
        finally:
            if proc is not None and proc.poll() is None:
                proc.kill()
                proc.wait()
            log.close()
            shutil.rmtree(d, ignore_errors=True)

    def item_c6_orig(self):
        """C6's ORIG on the device: each REF capture replayed every frame with the hash lists' flags, each frame's
        SHA-256 against its list; with --keep-differing N the first N differing frames of each capture are kept as
        PNG in the report's frames/ (game content: the report then stays on the owner's machines)."""
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import orig_compare
        keep = self.a.keep_differing
        machine = platform.machine().lower()
        reference_class = machine in REFERENCE_MACHINES
        baselines = ORIG_BASELINES.get(machine, {})
        res, verdicts = {}, []
        for cap, frames, listname, step in ORIG_CAPS:
            name = cap[:-4]
            if not os.path.isfile(self.cap(listname)) or not os.path.isfile(self.cap(cap)):
                res[name] = {"status": "missing", "note": f"needs {cap} and {listname} in the data folder"}
                continue
            ref = {}
            for line in open(self.cap(listname)):
                m = re.match(r"f_(\d+)\.ppm\s+([0-9A-Fa-f]{64})", line)
                if m:
                    ref[int(m.group(1))] = m.group(2).upper()
            refdir = self.cap(os.path.join(REF_DIR, name))
            refs = {int(m.group(1)) for m in (re.match(r"frame_(\d+)\.png$", f) for f in
                                              (os.listdir(refdir) if os.path.isdir(refdir) else [])) if m}
            baseline = baselines.get(name)
            got, differ, kept, compared, failing = {}, [], [], [], []
            per_frame = open(os.path.join(self.report, "logs", f"c6_orig_{name}_frames.jsonl"), "w")

            def keep_frame(k, path, name=name, kept=kept):
                if len(kept) < keep:
                    w, h, rgb = self.frames_rgb(path)
                    out = os.path.join(self.report, "frames", name)
                    os.makedirs(out, exist_ok=True)
                    self.write_png(os.path.join(out, f"frame_{k:06d}.png"), w, h, rgb)
                    kept.append(k)

            def on_frame(k, path, ref=ref, got=got, differ=differ, refs=refs, refdir=refdir, baseline=baseline,
                         compared=compared, failing=failing, per_frame=per_frame, step=step):
                data = open(path, "rb").read()
                got[k] = hashlib.sha256(data).hexdigest().upper()
                if k in ref and got[k] != ref[k]:
                    differ.append(k)
                    if not refs and k % step == 0:
                        keep_frame(k, path)   # (the routine run: the desktop compares them with its --make-refs)
                if k in refs:
                    r = orig_compare.compare_files(path, os.path.join(refdir, f"frame_{k:06d}.png"),
                                                   baseline=baseline)
                    compared.append(r)
                    per_frame.write(json.dumps(dict(r, frame=k)) + "\n")
                    if not r["pass"]:
                        failing.append(k)
                        keep_frame(k, path)
            try:
                code = self.streamed_hashes(cap, frames, name, on_frame)
            finally:
                per_frame.close()
            same = sum(1 for k in ref if got.get(k) == ref[k])
            missing = sorted(k for k in ref if k not in got)
            r = {"exit": code, "reference frames": len(ref), "identical": same, "differing": len(differ),
                 "first differing": differ[:20], "missing": len(missing), "kept": kept}
            if compared:
                shares = [c["histogram"] for c in compared]
                r.update({
                    "compared": len(compared), "reference frames on the device": len(refs),
                    "over the rule": len(failing), "first over": failing[:20],
                    "worst": max(c["worst"] for c in compared),
                    "mean shares": {b: round(sum(s[b] for s in shares) / len(shares), 3)
                                    for b in orig_compare.BUCKETS},
                    "1-2 share range": [min(s["1-2"] for s in shares), max(s["1-2"] for s in shares)],
                    "over-2 share range": [min(c["over_2_share"] for c in compared),
                                           max(c["over_2_share"] for c in compared)],
                    "most over 2 in an 8x8 block": max(c["over_2_busiest_8x8"] for c in compared),
                    "baselines": baseline,
                    "largest boundary step": max(c["steps_at_boundaries"] for c in compared)})
            if code != 0 or missing:
                verdict = "fail"
            elif reference_class:
                verdict = "pass" if same == len(ref) else "fail"
            elif compared:
                unjudged = len(compared) < len(refs)
                verdict = "fail" if failing or unjudged else "pass"   # (without baselines: the cap and the grid)
                r["shares judged"] = bool(baseline and "1-2" in baseline and "over_2" in baseline)
            else:
                verdict = "reported"   # no reference frames: the kept frames are compared on the desktop
            r["verdict"] = verdict
            res[name] = r
            verdicts.append(verdict)
        status = ("missing" if not verdicts else "fail" if "fail" in verdicts else
                  "reported" if "reported" in verdicts else "pass")
        return {"status": status, "numbers": dict(res, device_class=machine),
                "pass_line": "PLAN section 2's ORIG row: on x64 every frame identical to its hash list; on another "
                             "class (interim, until GS-exact texel sampling) no pixel over 32 levels from the "
                             "reference's frame, no 32-GS-pixel steps (band_measure.steps() on the difference: the "
                             "boundary median under 0.25 levels or at most 5 times the median elsewhere), and once "
                             "the capture has its class baselines, the 1-2 share within "
                             "5 points and the over-2 share within 2x of them",
                "note": ("per-frame numbers in logs/c6_orig_<capture>_frames.jsonl; 'reported': no reference frames "
                         "on the device (the kept frames are compared on the desktop); 'shares judged' false: the "
                         "capture's baselines aren't set yet (the every-frame run sets them)" +
                         ("; kept frames are game content: the report stays on the owner's machines" if keep else ""))}

    def item_c6_race7(self):
        ref = {}
        for line in open(self.cap("orig_r6_race7.sha256")):
            m = re.match(r"f_(\d+)\.ppm\s+([0-9A-Fa-f]{64})", line)
            if m:
                ref[int(m.group(1))] = m.group(2).upper()
        d = os.path.join(self.work, "race7_frames")
        shutil.rmtree(d, ignore_errors=True)
        os.makedirs(d)
        code, _ = self.replay("race7.cap", 100, ["--preset", "original", "--set", "debug.overlay=off"], "c6_race7",
                              capture=("1-100", d))
        got = {}
        for f in os.listdir(d):
            m = re.search(r"(\d+)\.ppm$", f)
            if m:
                got[int(m.group(1))] = hashlib.sha256(open(os.path.join(d, f), "rb").read()).hexdigest().upper()
        shutil.rmtree(d, ignore_errors=True)   # (frames of the game never leave the device)
        same = sum(1 for k in ref if got.get(k) == ref[k])
        differ = sorted(k for k in ref if k in got and got[k] != ref[k])
        missing = sorted(k for k in ref if k not in got)
        ok = code == 0 and same == len(ref)
        return {"status": "pass" if ok else "fail",
                "numbers": {"reference frames": len(ref), "identical": same, "differing": differ[:20],
                            "missing": missing[:20], "exit": code},
                "pass_line": "every frame equal to the x64 host's R6 hashes"}

    def scene_span(self, cap, frames, extra, name, skip_vblanks=0, skip_lines=0, env=None):
        """B6's span as lane B measured it: GT4_SCENE_CACHE_LOG=1's per-frame '(cpu X us)', the scene cache's own
        thread CPU for each scene frame (its stability check included, as in the desktop's numbers). Frames left
        out by the log's VBlank number (skip_vblanks) or by count from the first line (skip_lines: a capture that
        starts with a state save may not number its VBlanks from 0)."""
        e = {"GT4_SCENE_CACHE_LOG": "1", "GT4_PIPELINE_CACHE": "0"}
        e.update(env or {})
        code, out = self.replay(cap, frames, extra, name, env=e)
        cpu = [float(m.group(2)) for m in re.finditer(r"scene cache: vblank (\d+): .*\(cpu ([\d.]+) us\)", out)
               if int(m.group(1)) >= skip_vblanks]
        return code, cpu[skip_lines:]

    def item_b6_span(self):
        # (race_boot: START at VBlank 4075, its first 300 frames left out)
        code, cpu = self.scene_span("race_boot.cap", 4914, ["--preset", "high"], "b6_span", skip_vblanks=4375,
                                    env={"GT4_GSN_SCENE_SPLIT": "1"})
        if not cpu:
            return {"status": "error", "note": "no per-frame scene cache lines (GT4_SCENE_CACHE_LOG)",
                    "numbers": {"exit": code}}
        p95 = self.pct(cpu, 95)
        return {"status": "reported",
                "numbers": {"frames": len(cpu), "mean_us": round(statistics.mean(cpu), 1), "p95_us": p95,
                            "p99_us": self.pct(cpu, 99), "max_us": max(cpu), "margin_to_500_us": 500 - p95,
                            "desktop_p95_us": DESKTOP["b6_p95_us"], "desktop_commit": DESKTOP["commit"]},
                "pass_line": "within the budget the coordinator sets from this first measurement (the desktop's bar "
                             "is 500 us; the margin to it is reported)",
                "note": "lane B's method: GT4_GSN_SCENE_SPLIT=1, GT4_SCENE_CACHE_LOG=1 (its stability check "
                        "included), preset high, VBlanks from 4375; the desktop number is the same build's"}

    def item_night(self):
        """What choosing lighting.night = enhanced costs the front end: 'front end thread: busy' p95, enhanced
        against original. Enhanced also turns the scene split on (sceneSplitWanted) and original doesn't, so the
        added time includes the split's own cost, as F2b's +684 us did; B6's span is reported for enhanced only."""
        res = {}
        for mode in ("original", "enhanced"):
            prof = os.path.join(self.report, "logs", f"night_{mode}.jsonl")
            code, cpu = self.scene_span("lap_csr5.cap", 9000, ["--preset", "high", "--set", f"lighting.night={mode}",
                                                               "--profile-json", prof], f"night_{mode}",
                                        skip_lines=300)
            busy = self.stage(self.profile(prof, skip=300), "cpu_stages", "front end thread: busy", "cpu_us")
            res[mode] = {"exit": code, "front_end_p95_us": self.pct(busy, 95),
                         "front_end_mean_us": round(statistics.mean(busy), 1) if busy else None}
            if mode == "enhanced":
                res[mode]["b6_span_p95_us"] = self.pct(cpu, 95)
        a, b = res["original"]["front_end_p95_us"], res["enhanced"]["front_end_p95_us"]
        res["added_p95_us"] = round(b - a, 1) if a is not None and b is not None else None
        return {"status": "reported", "numbers": res,
                "pass_line": f"reported; the desktop's added p95: F2b's +684 us, and {DESKTOP['night_added_p95_us']} "
                             f"us on {DESKTOP['commit']}",
                "note": "the added time includes the scene split's own cost (enhanced turns it on, original doesn't); "
                        "it is the front end thread's wall clock, so a busy device spreads it (the desktop's two runs "
                        "under load differ tenfold); B6's span is thread CPU"}

    def frames_rgb(self, path):
        data = open(path, "rb").read()
        tokens, at = [], 0
        while len(tokens) < 4:
            while data[at:at + 1].isspace():
                at += 1
            end = at
            while not data[end:end + 1].isspace():
                end += 1
            tokens.append(data[at:end])
            at = end
        return int(tokens[1]), int(tokens[2]), data[at + 1:]

    def parity(self, preset, tag):
        """render.scene = hybrid against original on race_grid43 at `preset`, every 10th frame from frame 21 (it starts
        with a state save: the mesh cache warms up first, as PARITY is judged from frame 20), and the two threads' CPU
        p95. Frames are compared on the device and deleted."""
        dirs, cpu = {}, {}
        for scene in ("original", "hybrid"):
            d = os.path.join(self.work, f"{tag}_{scene}")
            shutil.rmtree(d, ignore_errors=True)
            os.makedirs(d)
            prof = os.path.join(self.report, "logs", f"{tag}_{scene}.jsonl")
            self.replay("race_grid43.cap", 700, ["--preset", preset, "--set", f"render.scene={scene}",
                                                 "--profile-json", prof], f"{tag}_{scene}", capture=("21-700/10", d))
            dirs[scene] = d
            frames = self.profile(prof, skip=20)
            cpu[scene] = {k: self.pct(self.stage(frames, "cpu_stages", n, "cpu_us"), 95) for k, n in
                          (("front_end_p95_us", "front end thread: busy"),
                           ("render_p95_us", "render: recording (uploads, scene, effects)"))}
        identical, within4 = [], []
        try:
            import numpy as np
        except ImportError:
            np = None
        for f in sorted(os.listdir(dirs["original"])):
            other = os.path.join(dirs["hybrid"], f)
            if not f.endswith(".ppm") or not os.path.isfile(other):
                continue
            w, h, a = self.frames_rgb(os.path.join(dirs["original"], f))
            _, _, b = self.frames_rgb(other)
            if np is not None:
                x = np.frombuffer(a, np.uint8).reshape(-1, 3).astype(np.int16)
                y = np.frombuffer(b, np.uint8).reshape(-1, 3).astype(np.int16)
                d = np.abs(x - y).max(axis=1)
                same, near = float((d == 0).mean()), float((d <= 4).mean())
            else:
                same = near = 0
                for i in range(0, len(a), 3):
                    m = max(abs(a[i] - b[i]), abs(a[i + 1] - b[i + 1]), abs(a[i + 2] - b[i + 2]))
                    same += m == 0
                    near += m <= 4
                same, near = same / (w * h), near / (w * h)
            identical.append(same)
            within4.append(near)
        for d in dirs.values():
            shutil.rmtree(d, ignore_errors=True)
        if not identical:
            return None
        return {"preset": preset, "frames": len(identical), "identical_min_pct": round(100 * min(identical), 3),
                "within4_min_pct": round(100 * min(within4), 3), "cpu": cpu,
                "pass": min(identical) >= 0.975 and min(within4) >= 0.9997}

    def item_hybrid(self):
        """PARITY's bar is defined at 1x; it is measured at preset high (the device's own setting, where the CPU cost
        matters), and, if it fails there, again at original before it is called a hybrid failure."""
        high = self.parity("high", "hybrid")
        if high is None:
            return {"status": "error", "note": "no frames to compare"}
        res = {"high": high}
        ok = high["pass"]
        if not ok:
            res["original"] = self.parity("original", "hybrid_original")
            ok = bool(res["original"] and res["original"]["pass"])
        return {"status": "pass" if ok else "fail", "numbers": res,
                "pass_line": "PARITY: >= 97.5 % identical and >= 99.97 % within 4 on every frame from 21; CPU reported",
                "note": "measured at high" + ("; failed there, so also at original (PARITY's 1x)" if "original" in res
                                              else "")}

    def item_pipeline_cache(self):
        cache = os.path.join(self.work, "pipeline_cache.bin")
        if os.path.exists(cache):
            os.remove(cache)
        res = {}
        for run in ("cold", "warm"):
            prof = os.path.join(self.report, "logs", f"pipeline_cache_{run}.jsonl")
            # (the driver's own disk cache off in both runs, Mesa's on the Odin, so only ours is measured)
            code, _ = self.replay("race7.cap", 100, ["--preset", "high", "--profile-json", prof],
                                  f"pipeline_cache_{run}", env={"GT4_PIPELINE_CACHE": cache,
                                                                "MESA_SHADER_CACHE_DISABLE": "true"})
            frames = self.profile(prof)
            draw = [float(f["draw_ms"]) for f in frames if "draw_ms" in f]
            res[run] = {"exit": code, "frames": len(draw), "draw_ms_max": max(draw) if draw else None,
                        "draw_ms_p99": self.pct(draw, 99), "first_10_draw_ms": [round(x, 1) for x in draw[:10]],
                        # (after the cold run: none means it wasn't written, and the warm run is a second cold one)
                        "cache_bytes_after": os.path.getsize(cache) if os.path.exists(cache) else None}
        if os.path.exists(cache):
            os.remove(cache)
        written = res["cold"]["cache_bytes_after"] is not None
        return {"status": "reported" if written else "error", "numbers": res,
                "pass_line": "the cold run's hitches reported, the warm run without",
                "note": "" if written else "the cold run wrote no cache file: the warm run was a second cold run"}

    def item_mixer(self):
        """The host mixer's time a frame on a replay. Needs a capture recorded with audio.mixer = host (MIXER_CAP):
        one recorded with the game mixing carries no records for the host's mixer. A headless replay makes the host
        mixer only with GT4_HOST_AUDIO_WAV set (main.cpp builds the audio output only when not headless), so each run
        writes its WAV into the work folder, and it is deleted at once: GT4's mixed sound never leaves the device."""
        if not os.path.isfile(self.cap(MIXER_CAP)):
            return {"status": "missing", "note": f"no {MIXER_CAP} in the data folder (a race recorded with "
                                              "audio.mixer = host)"}
        res = {}
        for spatial in ("original", "stereo", "hrtf"):
            sets = ["audio.mixer=host", "audio.interpolation=sinc", f"audio.spatial={spatial}"]
            extra = ["--preset", "original"] + [x for s in sets for x in ("--set", s)]
            wav = os.path.join(self.work, f"mixer_{spatial}.wav")
            try:
                code, out = self.replay(MIXER_CAP, MIXER_FRAMES, extra, f"mixer_{spatial}",
                                        env={"GT4_HOST_AUDIO_WAV": wav})
            finally:
                if os.path.exists(wav):
                    os.remove(wav)
            r = {"exit": code}
            m = re.search(r"host mixer: (\d+) frames, \d+ samples, (\d+) stream 1 values unmatched, the mix "
                          r"([\d.]+) us a frame \(at most ([\d.]+)\)", out)
            if m:   # (stream 1 values unmatched: the replay's stand-in for an underrun, a mix that didn't keep up)
                r.update({"frames": int(m.group(1)), "unmatched": int(m.group(2)), "mix_us": float(m.group(3)),
                          "mix_max_us": float(m.group(4))})
            f = re.search(r"(\d+) frames without", out)
            if f:
                r["frames_without_tags"] = int(f.group(1))
            h = re.search(r"hrtf: ([\d.]+) buses a frame \(at most (\d+)\), ([\d.]+) us a frame "
                          r"\(at most ([\d.]+)\)", out)
            if h:
                r.update({"hrtf_buses": float(h.group(1)), "hrtf_buses_max": int(h.group(2)),
                          "hrtf_us": float(h.group(3)), "hrtf_us_max": float(h.group(4))})
            if re.search(r"--set audio\.spatial=\w+: expected", out):
                r["note"] = "this build refuses the value"
            res[spatial] = r
        none = all(not r.get("frames") for r in res.values())
        return {"status": "error" if none else "reported", "numbers": res,
                "note": "no host mixer summary in any run" if none else
                        "underruns: a device item (a live race with sound); 'unmatched' is the replay's stand-in",
                "pass_line": "no underrun in a 5-minute race at each (device); hrtf's time against the frame"}

    def game_log(self):
        p = os.path.join(self.package, "game.log")
        return p, (os.path.getsize(p) if os.path.isfile(p) else 0)

    def live_game_args(self):
        """The host's arguments that start the package's game in a session: with --session-dir the host doesn't
        look for a packaged game itself (main.cpp's usePackagedGame: a session is the test scene unless --game says
        otherwise). The game folder's files are passed as the game sees them: under Proton's Wine (Linux) as Z:\\
        paths, as the host's gamePathArgument makes them. (None, why) when the package lacks one."""
        game_dir = os.path.join(self.package, "game")
        isos = sorted(glob.glob(os.path.join(game_dir, "*.iso")))
        elf = os.path.join(game_dir, "CORE.GT4.elf")
        exe = os.path.join(self.package, "gt4_game.exe")
        missing = [n for n, ok in (("an .iso in game/", isos), ("game/CORE.GT4.elf (made at the first start)",
                                                                 os.path.isfile(elf)),
                                   ("gt4_game.exe", os.path.isfile(exe))) if not ok]
        if missing:
            return None, "the package lacks " + ", ".join(missing)
        wine = (lambda p: p) if os.name == "nt" else (lambda p: "Z:" + p.replace("/", "\\"))
        log = os.path.join(self.package, "game.log")
        return ["--game", exe, "--game-args",
                f'--elf "{wine(elf)}" --iso "{wine(isos[0])}" --log "{wine(log)}"'], ""

    def item_load_times(self):
        route_file = os.path.join(ROOT, ROUTE)
        if not os.path.isfile(route_file):
            return {"status": "error", "note": f"no route {route_file}"}
        live, why = self.live_game_args()
        if not live:
            return {"status": "error", "note": why}
        saves = self.save_copy("saves-pe-race2")
        session = os.path.join(self.work, "load_session")
        shutil.rmtree(session, ignore_errors=True)
        os.makedirs(os.path.join(session, "commands"))
        cmds = [l.strip() for l in open(route_file) if l.strip() and not l.startswith("#")]
        cmds = [c for c in cmds if not c.startswith("capture ")] + ["vwait 600", "quit"]
        for k, c in enumerate(cmds):
            open(os.path.join(session, "commands", f"{k:020d}-route.cmd"), "w").write(c + "\n")
        glog, offset = self.game_log()
        env = dict(os.environ, GT4_LOAD_STATS="1")
        cmd = [self.launcher, "--headless", "--no-audio", "--size", "640x448", "--session-dir", session, "--saves",
               saves, "--preset", "original"] + FIXED + live
        try:
            r = subprocess.run(cmd, capture_output=True, text=True, env=env, cwd=self.package, timeout=1800,
                               errors="replace")
            out, code = r.stdout + r.stderr, r.returncode
        except subprocess.TimeoutExpired as x:
            out, code = (x.stdout or "") + "\n(timed out)", -1
        game = ""
        if os.path.isfile(glog):
            with open(glog, "rb") as f:
                f.seek(offset)
                game = f.read().decode("utf-8", "replace")
        self.log("load_times.log", f"$ {' '.join(cmd)}\nexit {code}\n{out}\n--- game.log (this run)\n{game}")
        shutil.rmtree(saves, ignore_errors=True)
        shutil.rmtree(session, ignore_errors=True)   # (its frames folder: no captures were asked, but never keep it)
        loads = [{"vblanks": int(m.group(1)), "seconds": float(m.group(2)), "blocked": int(m.group(3)),
                  "blocked_s": float(m.group(4))}
                 for m in re.finditer(r"load: VBlanks \d+-\d+ \((\d+), ([\d.]+) s\).*?blocked (\d+) \(([\d.]+) s\)",
                                      game + out)]
        if not loads:
            return {"status": "error", "numbers": {"exit": code}, "note": "no 'load:' lines (GT4_LOAD_STATS)"}
        race = max(loads, key=lambda l: l["seconds"])   # (the route's longest load is the race's)
        return {"status": "pass" if race["blocked_s"] < 0.2 else "fail",
                "numbers": {"loads": loads, "race_load": race, "exit": code},
                "pass_line": "the race load's blocked time under 0.2 s (D10 (c))"}

    def item_photo_travel(self):
        game_dir = os.path.join(self.package, "game")
        isos = glob.glob(os.path.join(game_dir, "*.iso"))
        elf = os.path.join(game_dir, "CORE.GT4.elf")
        exe = os.path.join(self.package, "gt4_game.exe")
        if not isos or not os.path.isfile(elf) or not os.path.isfile(exe):
            return {"status": "error", "note": f"needs {game_dir} with the .iso and CORE.GT4.elf, and gt4_game.exe"}
        orig = os.path.join(self.work, "orig")   # (photo_check's names, as links)
        shutil.rmtree(orig, ignore_errors=True)
        os.makedirs(orig)
        os.symlink(elf, os.path.join(orig, "CORE.GT4.elf"))
        os.symlink(isos[0], os.path.join(orig, "Gran Turismo 4 (USA) (v1.01).iso"))
        saves = self.save_copy("r5_saves")
        work = os.path.join(self.work, "photo")
        cmd = [sys.executable, os.path.join(ROOT, "tools", "photo_check.py"), "--host", self.launcher, "--game", exe,
               "--orig", orig, "--saves", saves, "--work", work, "--no-slots", "--keep"]
        try:
            r = subprocess.run(cmd, capture_output=True, text=True, timeout=1800, errors="replace",
                               env=dict(os.environ, GT4_READ_HASH="0"))
            out, code = r.stdout + r.stderr, r.returncode
        except subprocess.TimeoutExpired as x:
            out, code = (x.stdout or "") + "\n(timed out)", -1
        host_log = os.path.join(work, "host.log")
        lines = open(host_log, encoding="utf-8", errors="replace").read() if os.path.isfile(host_log) else ""
        # ('gsn read N took X ms' is printed only for a read over 16 ms; the count of all of them is the GS summary's)
        reads = [float(m.group(1)) for m in re.finditer(r"gsn read \d+ took ([\d.]+) ms", lines)]
        total = re.findall(r"(\d+) reads of drawn pixels from the GPU", lines)
        self.log("photo_travel.log", f"$ {' '.join(cmd)}\nexit {code}\n{out}\n--- host.log's read lines\n" +
                 "\n".join(l for l in lines.splitlines()
                           if "gsn read" in l or "photo" in l.lower() or "reads of drawn pixels" in l))
        for d in (work, saves, orig):   # (the photo view's frames and the save copy stay on no disk)
            shutil.rmtree(d, ignore_errors=True)
        captures = re.search(r"captures ([\d., ]+),", out)
        ok = code == 0 and (not reads or max(reads) <= 1000.0)
        return {"status": "pass" if ok else "fail",
                "numbers": {"exit": code, "gpu_reads": int(total[-1]) if total else None,
                            "slow_reads_over_16ms": len(reads), "read_ms_max": max(reads) if reads else None,
                            "slow_reads_ms_total": round(sum(reads), 1) if reads else None,
                            "photo_captures": captures.group(1) if captures else None},
                "pass_line": "photo_check OK (the photo developed within PHOTO_TRIES x 600 VBlanks) and no read "
                             "over 1 s"}

    def item_night_bands(self):
        """The night bands' triage (C6b; tools/band_measure.py, lane F's): both night laps replayed at the owner's
        settings (the package's settings.ini when there is one, else ultra) at the Odin's 1280x800, every 100th frame
        from 600 measured on the device and deleted. A score is 'look at this frame', never a verdict: compare it with
        the desktop's for the same replay frame."""
        settings = os.path.join(self.package, "settings.ini")
        chosen = (["--settings", settings] if os.path.isfile(settings) else ["--preset", "ultra"])
        res = {"settings": settings if os.path.isfile(settings) else "preset ultra (no settings.ini in the package)"}
        for cap, frames in NIGHT_LAPS:
            d = os.path.join(self.work, "bands_" + cap[:-4])
            shutil.rmtree(d, ignore_errors=True)
            os.makedirs(d)
            cmd = [self.host, "--headless", "--size", "1280x800", "--replay", self.cap(cap), "--frames", str(frames),
                   "--capture-frames", f"600-{frames}/100", "--capture-dir", d] + chosen + FIXED
            r = subprocess.run(cmd, capture_output=True, text=True, cwd=self.package, timeout=3600, errors="replace")
            m = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "band_measure.py"), "--json", d],
                               capture_output=True, text=True, timeout=3600, errors="replace")
            shutil.rmtree(d, ignore_errors=True)   # (frames of the game never leave the device)
            scores = []
            for line in m.stdout.splitlines():
                try:
                    scores.append(json.loads(line))
                except ValueError:
                    pass
            self.log(f"night_bands_{cap[:-4]}.jsonl", m.stdout)
            self.log(f"night_bands_{cap[:-4]}.log", f"$ {' '.join(cmd)}\nexit {r.returncode}\n{r.stdout}{r.stderr}\n"
                                                    f"--- band_measure\n{m.stderr}")
            top = sorted(scores, key=lambda s: -s.get("score", 0))[:8]
            res[cap[:-4]] = {"exit": r.returncode, "frames": len(scores),
                             "max_score": top[0]["score"] if top else None,
                             "top": [{"frame": s["frame"], "score": s["score"], "runs": s["runs"][:3]} for s in top]}
        measured = any(res[c[:-4]]["frames"] for c, _ in NIGHT_LAPS)
        return {"status": "reported" if measured else "error", "numbers": res,
                "pass_line": "triage only: frames scoring above the desktop's same replay frames are to be looked "
                             "at (lane F's desktop noise floor: lap_ssr5r p95 0.011, max 0.024; lap_csr5 p95 0.036, "
                             "max 0.048)",
                "note": "scores per frame in logs/night_bands_*.jsonl; the device's live dump (L3 + R3) is the other "
                        "half of the item"}

    def run(self, items):
        problems, free = self.preflight(items)
        self.results["preflight"] = {"free_gb": round(free, 1), "problems": problems}
        if problems:
            self.write()
            print("device_run: not started:\n  " + "\n  ".join(problems))
            return 2
        for name in items:
            print(f"device_run: {name} ...", flush=True)
            t = time.time()
            try:
                res = getattr(self, "item_" + name)()
            except Exception as e:   # (one item's failure never stops the others)
                res = {"status": "error", "note": f"{type(e).__name__}: {e}"}
            res["seconds"] = round(time.time() - t)
            self.results[name] = res
            print(f"device_run: {name}: {res['status']} ({res['seconds']} s)", flush=True)
            self.write()
        for name, why in PENDING.items():
            self.results[name] = {"status": "pending", "note": why}
        self.write()
        shutil.rmtree(self.work, ignore_errors=True)
        bad = [r for r in self.results.values() if isinstance(r, dict) and r.get("status") in ("fail", "error")]
        return 1 if bad else 0

    def write(self):
        with open(os.path.join(self.report, "report.json"), "w", encoding="utf-8") as f:
            json.dump(self.results, f, indent=1, default=str)
        lines = ["# Odin device run (PLAN.md C6b)", "", f"{datetime.datetime.now():%Y-%m-%d %H:%M}, package "
                 f"{self.package}", "", "| Item | Status | Numbers | Pass line | Note |", "|---|---|---|---|---|"]
        for name, r in self.results.items():
            if name == "preflight":
                lines.insert(4, f"Free space at the start: {r['free_gb']} GB." +
                             (" Not started: " + "; ".join(r["problems"]) if r["problems"] else "") + "\n")
                continue
            nums = json.dumps(r.get("numbers", {}), default=str).replace("|", "/")
            lines.append(f"| {name} | {r.get('status')} | {nums} | {r.get('pass_line', '')} | {r.get('note', '')} |")
        with open(os.path.join(self.report, "report.md"), "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")


def make_hashes(host, capture, out):
    """A capture's hash list on this machine, as lane B's R6 lists are made (ORIG_FLAGS, every frame, 'f_N.ppm SHA'
    lines): the desktop's reference that c6_orig compares the device with for a capture that has no R6 list."""
    import tempfile

    class Args:
        data = os.path.dirname(os.path.abspath(capture))
        package = os.path.dirname(os.path.abspath(host))
        report = tempfile.mkdtemp(prefix="gt4_hashes_")
        keep_differing = 0
        min_free_gb = 3.0
    dev = Device(Args)
    dev.host = os.path.abspath(host)
    frames = None
    for cap, n, _, _ in ORIG_CAPS:
        if cap == os.path.basename(capture):
            frames = n
    if frames is None:
        sys.exit(f"device_run: {capture} is not one of ORIG_CAPS")
    hashes = {}

    def on_frame(k, path):
        hashes[k] = hashlib.sha256(open(path, "rb").read()).hexdigest().upper()
    code = dev.streamed_hashes(os.path.basename(capture), frames, "hashes", on_frame)
    with open(out, "w", newline="\n") as f:
        f.write(f"# {os.path.basename(capture)} original 1280x960 every 1 frame(s), host {host}\n")
        for k in sorted(hashes):
            f.write(f"f_{k:06d}.ppm {hashes[k]}\n")
    shutil.rmtree(Args.report, ignore_errors=True)
    print(f"device_run: {len(hashes)} frames hashed into {out} (host exit {code})")
    return code


def make_refs(host, data, out):
    """The reference class's frames for c6_orig's pixel comparison (PLAN section 2's ORIG row; the coordinator's
    choice (b), 2026-11-09): each ORIG_CAPS capture in DATA replayed on this machine (x64, the reference class) with
    ORIG_FLAGS, its frames at the capture's step written as OUT/<capture>/frame_N.png, each one first checked
    against the capture's hash list in DATA (a frame that doesn't match is not written, and the run fails: OUT would
    not be the reference). The coordinator copies OUT to the device's DIR/ref/. Game content: the owner's machines
    only, never committed or served."""
    import tempfile
    if platform.machine().lower() not in REFERENCE_MACHINES:
        sys.exit(f"device_run: the reference frames are made on the reference class (x64), not {platform.machine()}")

    class Args:
        package = os.path.dirname(os.path.abspath(host))
        report = tempfile.mkdtemp(prefix="gt4_refs_")
        keep_differing = 0
        min_free_gb = 3.0
    Args.data = os.path.abspath(data)
    dev = Device(Args)
    dev.host = os.path.abspath(host)
    bad = 0
    for cap, frames, listname, step in ORIG_CAPS:
        name = cap[:-4]
        if not os.path.isfile(dev.cap(cap)) or not os.path.isfile(dev.cap(listname)):
            print(f"device_run: {name}: {cap} or {listname} not in {data}, skipped")
            bad += 1
            continue
        ref = {}
        for line in open(dev.cap(listname)):
            m = re.match(r"f_(\d+)\.ppm\s+([0-9A-Fa-f]{64})", line)
            if m:
                ref[int(m.group(1))] = m.group(2).upper()
        d = os.path.join(out, name)
        os.makedirs(d, exist_ok=True)
        written, mismatched, size = [], [], [0]

        def on_frame(k, path, ref=ref, d=d, written=written, mismatched=mismatched, size=size):
            if k not in ref:
                return   # (the lists end a frame before the replay: race7's 100, race_grid43's 700 aren't hashed)
            if hashlib.sha256(open(path, "rb").read()).hexdigest().upper() != ref[k]:
                mismatched.append(k)
                return
            w, h, rgb = dev.frames_rgb(path)
            png = os.path.join(d, f"frame_{k:06d}.png")
            Device.write_png(png, w, h, rgb, up=True)
            written.append(k)
            size[0] += os.path.getsize(png)
        frame_list = f"1-{frames}" if step == 1 else f"{step}-{frames}/{step}"
        code = dev.streamed_hashes(cap, frames, "refs_" + name, on_frame, capture_list=frame_list)
        expected = sum(1 for k in range(1 if step == 1 else step, frames + 1, step) if k in ref)
        bad += code != 0 or bool(mismatched) or len(written) != expected
        print(f"device_run: {name}: {len(written)} of {expected} reference frames written ({size[0]:,} bytes), "
              f"{len(mismatched)} not matching the hash list {mismatched[:10]}, host exit {code}")
    shutil.rmtree(Args.report, ignore_errors=True)
    total = sum(os.path.getsize(os.path.join(r, f)) for r, _, fs in os.walk(out) for f in fs)
    print(f"device_run: {out}: {total:,} bytes in all")
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--ssh", help="the device's ssh destination (desktop side)")
    ap.add_argument("--remote", default="~/gt4-device", help="the data folder on the device")
    ap.add_argument("--package", default="~/Games/GT4-Native-linux", help="the Odin build's folder on the device")
    ap.add_argument("--items", default="", help="comma-separated items (default: all)")
    ap.add_argument("--out", default="", help="where the report comes back to (desktop side)")
    ap.add_argument("--min-free-gb", type=float, default=3.0)
    ap.add_argument("--keep-differing", type=int, default=0, help="c6_orig: keep the first N frames of each capture "
                    "as PNG in report/frames/<capture>/: without reference frames on the device, differing ones at the "
                    "references' step (for tools/orig_compare.py on the desktop); with them, those over the rule "
                    "(game content: the report then stays on the owner's machines)")
    ap.add_argument("--make-hashes", nargs=3, metavar=("HOST", "CAPTURE", "OUT"),
                    help="write CAPTURE's hash list with ORIG_FLAGS, every frame, as lane B's lists (the desktop's "
                         "reference for a capture with no R6 list, such as race_pe2_prog)")
    ap.add_argument("--make-refs", nargs=3, metavar=("HOST", "DATA", "OUT"),
                    help="write the reference class's frames for c6_orig (x64 only): each ORIG_CAPS capture in DATA "
                         "at its step as OUT/<capture>/frame_N.png, checked against its hash list; OUT goes to the "
                         "device's DIR/ref/")
    ap.add_argument("--device", action="store_true", help="the device half (run by the desktop side over ssh)")
    ap.add_argument("--data", default="~/gt4-device")
    ap.add_argument("--report", default="")
    a = ap.parse_args()
    items = [i for i in a.items.split(",") if i] if a.items else list(ITEMS)
    unknown = [i for i in items if i not in ITEMS]
    if unknown:
        ap.error(f"unknown items {unknown}; known: {', '.join(ITEMS)}")
    if a.make_hashes:
        sys.exit(make_hashes(*a.make_hashes))
    if a.make_refs:
        sys.exit(make_refs(*a.make_refs))
    if a.device:
        if not a.report:
            ap.error("--device needs --report")
        sys.exit(Device(a).run(items))
    if not a.ssh:
        ap.error("--ssh HOST (or --device on the device)")
    desktop(a)


if __name__ == "__main__":
    main()
