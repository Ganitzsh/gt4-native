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
    provoking       VK_EXT_provoking_vertex on the device (vulkaninfo; B7, C6)
    c6_race7        race7.cap replayed at 1280x960, original: every frame's SHA-256 against the x64 hashes (C6)
    b6_span         B6's scene-cache span on race_boot.cap after its first 300 frames, per frame, p95 (B6c)
    night           the same span on the night lap with lighting.night = enhanced against original (F2b)
    hybrid          render.scene = hybrid against original on race_grid43.cap, every 10th frame: PARITY's two
                    shares, and the front-end and render threads' CPU from --profile-json (B8)
    pipeline_cache  race7.cap twice with GT4_PIPELINE_CACHE in the work folder, emptied first: the cold run's frame
                    times against the warm one's (B2)
    mixer           audio.mixer = host, audio.interpolation = sinc, at audio.spatial original, stereo and hrtf: the
                    host summary's mix time (and hrtf's buses) on a replay (D5, D9); underruns are a device item
    load_times      GT4_LOAD_STATS=1 on the route to PE race 2 (docs/modern/routes/race_gt_pe2.txt) with a copy of
                    saves-pe-race2: each load's blocked VBlanks and time; the race load against 0.2 s (D10 (c))
    photo_travel    tools/photo_check.py with a copy of r5_saves: the shot's reads and time (lane A's CT24 fix)

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
TOOLS = ("device_run.py", "photo_check.py", "perf_report.py")
ROUTE = "docs/modern/routes/race_gt_pe2.txt"

# The files the device half reads in DIR (C6b; the coordinator copies them), with their sizes in bytes.
DATA = {
    "race7.cap": 119256448,
    "race_grid43.cap": 320675952,
    "race_boot.cap": 2252306688,
    "lap_csr5.cap": 3888681808,
    "orig_r6_race7.sha256": 7949,
    "orig_r6_race_grid43.sha256": 55355,
}
SAVES = ("saves-pe-race2", "r5_saves")
# The mixer row's capture: a race recorded with audio.mixer = host (lane D's; its name and size are added to DATA once
# the coordinator copies it). Without it the row says so.
MIXER_CAP = "race_mixer_host.cap"
MIXER_FRAMES = 9600   # its VBlanks (lane D's recording; set to the REF capture's count when it is made): a replay
                      # doesn't end by itself at the capture's end, so --frames is the capture's length
ITEMS = ("env", "provoking", "c6_race7", "b6_span", "night", "hybrid", "pipeline_cache", "mixer", "load_times",
         "photo_travel")
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
                 "load_times": ("saves-pe-race2",), "photo_travel": ("r5_saves",)}
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
        return {"status": "reported", "numbers": {"VK_EXT_provoking_vertex": present,
                                                    "provokingVertexLast": m.group(1) if m else None},
                "note": "absent is a B7 finding, not a C6 failure"}

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

    def item_load_times(self):
        route_file = os.path.join(ROOT, ROUTE)
        if not os.path.isfile(route_file):
            return {"status": "error", "note": f"no route {route_file}"}
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
               saves, "--preset", "original"] + FIXED
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


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--ssh", help="the device's ssh destination (desktop side)")
    ap.add_argument("--remote", default="~/gt4-device", help="the data folder on the device")
    ap.add_argument("--package", default="~/Games/GT4-Native-linux", help="the Odin build's folder on the device")
    ap.add_argument("--items", default="", help="comma-separated items (default: all)")
    ap.add_argument("--out", default="", help="where the report comes back to (desktop side)")
    ap.add_argument("--min-free-gb", type=float, default=3.0)
    ap.add_argument("--device", action="store_true", help="the device half (run by the desktop side over ssh)")
    ap.add_argument("--data", default="~/gt4-device")
    ap.add_argument("--report", default="")
    a = ap.parse_args()
    items = [i for i in a.items.split(",") if i] if a.items else list(ITEMS)
    unknown = [i for i in items if i not in ITEMS]
    if unknown:
        ap.error(f"unknown items {unknown}; known: {', '.join(ITEMS)}")
    if a.device:
        if not a.report:
            ap.error("--device needs --report")
        sys.exit(Device(a).run(items))
    if not a.ssh:
        ap.error("--ssh HOST (or --device on the device)")
    desktop(a)


if __name__ == "__main__":
    main()
