#!/usr/bin/env python3
"""Run commands while holding machine-wide slots (lock folders), without a shell.

    py -I tools/slot.py --slot SLOT[+SLOT...] --what TEXT --log FILE [--cwd DIR] [--env K=V ...] [--min-gb N]
                        -- CMD ARGS [--- CMD ARGS ...]

SLOT: game (gt4-decomp-release and one of heavy-1 and heavy-2: game builds), heavy (heavy-1 or heavy-2: host builds,
live games, replays), heavy-both (both: a replay over 2 GB), live-game (the console's focus: a windowed game or
PCSX2); docs/modern/CAPTURES.md lists them. A game build takes a heavy slot too, so at most two of the big jobs run
at once: two heavy jobs, or a game build and one (2026-11-09: a game build, a host build and a live game started
within 90 s, each past the floor before the others had grown, and the machine ran out of memory). The floor is
looked at once, when a job starts; it can't see a job that started a moment before and hasn't reached its peak.
A slot is a folder under GT4_LOCKS (default: ~/locks), made with mkdir (atomic); owner.txt in it says who holds it,
its first line for people, its second "pid=N start=T" (the holder and its start time) for the waiters.

Waits for its turn, until N GB are available (default 8) and every slot asked for is free, takes them together (a
partial hold is given back, never kept while waiting), runs each command in turn (stops at the first failure), and
removes the slots on exit.

WSL's memory (Windows): WSL's VM keeps Linux's freed memory and page cache from Windows. After a job that ran in WSL
(wsl.exe, or tools/build_linux.sh), and when a waiter's turn has come but the memory is under its floor while the VM
holds over WSL_DROP_ABOVE_GB, Linux's caches are dropped and its memory compacted (as root; nothing is killed), at most
every WSL_DROP_EVERY_S for a waiter, logged with what the VM held before and after. A VM not running is left alone.
The waiting line says the memory available and what the VM holds.

Turns: a waiter puts a ticket in GT4_LOCKS/queue (named by its arrival time; its lines the people's line, the holder
line, and the slots it waits for) and takes nothing while an older live ticket waits for one of the same slots. So
waiters are served in the order they came, the memory included: the oldest waits for the floor, and a job started
later doesn't pass it. A ticket goes when its slots are taken, and a stale one (its waiter gone) is removed, as a
stale slot is. A runner older than the queue takes no ticket and is not held back by one.

Self-healing: a waiter finding a slot whose holder is gone (its PID dead, or reused by a process started at another
time) logs it and removes it, under a lock the OS frees with its holder (Windows: a named mutex; elsewhere flock), so
two waiters never remove each other's fresh slot. On Windows the holder and everything it starts share a job object
that kills them all when the holder dies, so a slot freed that way has no work still running under it.

    tools/slot.py --self-test       the stale-holder checks on a temporary lock folder (no slots touched)

Scripts can hold slots themselves: `with slot.held(["heavy", "live-game"], "what") as names: ...`.
"""
import argparse
import contextlib
import ctypes
import datetime
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time

WINDOWS = os.name == "nt"
SLOTS = {"game": [["gt4-decomp-release", "heavy-1"], ["gt4-decomp-release", "heavy-2"]],   # (a heavy slot too)
         "heavy": [["heavy-1"], ["heavy-2"]], "heavy-both": [["heavy-1", "heavy-2"]], "live-game": [["live-game"]]}

# Low memory: off. On only by the coordinator's word, while the machine's available memory sits under 7 GB (a kernel
# leak only the owner's reboot clears; on from 2026-11-08 to the reboot of 2026-11-09). When on, the floors are capped
# per kind and the heavy jobs run one at a time, never beside a game build: heavy takes only heavy-1, and game and
# heavy also take the low-memory slot; heavy-both is refused (take(): its 7 GB is above the memory there is, and its
# ticket would hold back every heavy job and game build; its row below is never reached). The caps: the largest heavy
# job's own peak measured (a clean host build, 1.27 GB; a game link 1.10, a replay 1.21) plus 1 GB is under the
# 4.5 GB minimum, so heavy 4.5; game and live-game 5.5. A job's floor is min(--min-gb, the largest cap among its
# kinds), live-game counting only alone.
LOW_MEMORY = False
FLOOR_CAPS = {"game": 5.5, "live-game": 5.5, "heavy": 4.5, "heavy-both": 7.0}
LOW_MEMORY_SLOTS = {"game": [["gt4-decomp-release", "low-memory"]], "heavy": [["heavy-1", "low-memory"]],
                    "heavy-both": [["heavy-1", "heavy-2", "low-memory"]], "live-game": [["live-game"]]}


def slots_table():
    return LOW_MEMORY_SLOTS if LOW_MEMORY else SLOTS


def locks_dir():
    return os.environ.get("GT4_LOCKS") or os.path.join(os.path.expanduser("~"), "locks")


def log_line(text):
    print(f"slot: {time.strftime('%H:%M:%S')} {text}", file=sys.stderr, flush=True)


# ---- processes: alive, and their start time (a PID reused by another process has another start time)

UNKNOWN = -1   # process_start: the process runs, but this process may not read its start time (kept as alive)


def process_start(pid):
    """The process's start time (an integer), None when no such process runs, or UNKNOWN when it runs but cannot be
    opened (another user's or a protected process: never taken for gone)."""
    if pid <= 0:
        return None
    if WINDOWS:
        k32 = ctypes.WinDLL("kernel32", use_last_error=True)
        k32.OpenProcess.restype = ctypes.c_void_p
        h = k32.OpenProcess(0x1000, False, pid)   # PROCESS_QUERY_LIMITED_INFORMATION
        if not h:
            return None if ctypes.get_last_error() == 87 else UNKNOWN   # ERROR_INVALID_PARAMETER: no such process
        try:
            code = ctypes.c_ulong()
            if k32.GetExitCodeProcess(ctypes.c_void_p(h), ctypes.byref(code)) and code.value != 259:   # STILL_ACTIVE
                return None
            times = [ctypes.c_ulonglong() for _ in range(4)]
            if not k32.GetProcessTimes(ctypes.c_void_p(h), *[ctypes.byref(t) for t in times]):
                return UNKNOWN
            return times[0].value   # creation, FILETIME
        finally:
            k32.CloseHandle(ctypes.c_void_p(h))
    try:
        with open(f"/proc/{pid}/stat") as f:
            return int(f.read().rsplit(")", 1)[1].split()[19])   # starttime, clock ticks since boot
    except FileNotFoundError:
        return None
    except (OSError, IndexError, ValueError):
        return UNKNOWN


def holder_line():
    return f"pid={os.getpid()} start={process_start(os.getpid())}"


def read_holder(path):
    """(pid, start, first line) from a slot's owner.txt; pid None when it has no holder line (an older runner)."""
    try:
        with open(os.path.join(path, "owner.txt"), encoding="utf-8", errors="replace") as f:
            lines = f.read().splitlines()
    except OSError:
        return None, None, ""
    for line in lines[1:]:
        fields = dict(w.split("=", 1) for w in line.split() if "=" in w)
        if "pid" in fields:
            try:
                start = fields.get("start", "None")
                return int(fields["pid"]), None if start == "None" else int(start), lines[0]
            except ValueError:
                break
    return None, None, lines[0] if lines else ""


def stale(path):
    """Why the slot's holder is gone, or None while it may still hold it (alive, or unknown: no holder line)."""
    pid, start, _ = read_holder(path)
    if pid is None:
        return None
    now = process_start(pid)
    if now == UNKNOWN:
        return None
    if now is None:
        return f"its holder (pid {pid}) is not running"
    if start is not None and now != start:
        return f"its holder's pid {pid} now belongs to another process"
    return None


# ---- the reaping lock: freed by the OS with its holder

@contextlib.contextmanager
def reap_lock():
    if WINDOWS:
        k32 = ctypes.windll.kernel32
        k32.CreateMutexW.restype = ctypes.c_void_p
        name = "Local\\gt4-slots-reap-" + os.path.abspath(locks_dir()).replace("\\", "/").replace(":", "").lower()
        h = k32.CreateMutexW(None, False, name[:250])
        if not h:
            raise OSError("CreateMutexW failed")
        r = k32.WaitForSingleObject(ctypes.c_void_p(h), 10000)
        if r not in (0, 0x80):   # WAIT_OBJECT_0, WAIT_ABANDONED (its holder died: ours now)
            k32.CloseHandle(ctypes.c_void_p(h))
            raise TimeoutError("the reaping lock")
        try:
            yield
        finally:
            k32.ReleaseMutex(ctypes.c_void_p(h))
            k32.CloseHandle(ctypes.c_void_p(h))
    else:
        import fcntl
        os.makedirs(locks_dir(), exist_ok=True)
        with open(os.path.join(locks_dir(), ".reap.lock"), "w") as f:
            fcntl.flock(f, fcntl.LOCK_EX)
            yield


def reap(name):
    """Remove the slot `name` if its holder is gone (checked again under the reaping lock). True if removed."""
    path = os.path.join(locks_dir(), name)
    if not os.path.isdir(path) or not stale(path):
        return False
    with reap_lock():
        why = stale(path)
        if not why:
            return False
        _, _, first = read_holder(path)
        shutil.rmtree(path, ignore_errors=True)
    log_line(f"removed the stale slot {name}: {why} ({first.strip()})")
    return True


# ---- taking and giving back

def claim(name, what):
    path = os.path.join(locks_dir(), name)
    try:
        os.mkdir(path)
    except OSError:
        return None
    lane = os.path.basename(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    with open(os.path.join(path, "owner.txt"), "w", encoding="utf-8") as f:
        when = datetime.datetime.now().astimezone().isoformat(timespec="seconds")
        f.write(f"{lane} {what} {when}\n{holder_line()}\n")
    return path


def available_gb():
    if WINDOWS:
        class MEMORYSTATUSEX(ctypes.Structure):
            _fields_ = [("dwLength", ctypes.c_ulong), ("dwMemoryLoad", ctypes.c_ulong)] + \
                       [(n, ctypes.c_ulonglong) for n in ("total", "avail", "tpf", "apf", "tv", "av", "aev")]
        m = MEMORYSTATUSEX()
        m.dwLength = ctypes.sizeof(m)
        ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(m))
        return m.avail / 2**30
    with open("/proc/meminfo") as f:
        for line in f:
            if line.startswith("MemAvailable:"):
                return int(line.split()[1]) / 2**20
    return 0.0


# ---- WSL's memory (the coordinator, 2026-11-10): WSL's VM (vmmemWSL) keeps what Linux freed and its page cache on the
# Windows side, and gives none of it back by itself (no autoMemoryReclaim in the owner's .wslconfig): at 03:40 it held
# 19.3 GB with about 1 GB used inside, the desktop had 0.2 GB free, and jobs stopped. Dropping Linux's caches and
# compacting its memory (as root, killing nothing) let it fall to 8.8 GB. slot.py does that after a job that ran in WSL
# (the Linux and arm64 host builds), and before taking slots when the memory is under the floor and WSL holds much.

WSL_DROP = "sync; echo 3 > /proc/sys/vm/drop_caches; echo 1 > /proc/sys/vm/compact_memory"
WSL_DROP_ABOVE_GB = 4.0     # a waiter under its floor drops WSL's caches only when vmmemWSL holds more than this
WSL_DROP_EVERY_S = 300      # ... and at most this often
_wsl_looked_at = [0.0]


def is_wsl_command(cmd):
    """Whether a command runs in WSL: wsl(.exe) itself, or tools/build_linux.sh (the Linux and arm64 host builds)."""
    return bool(cmd) and (os.path.basename(cmd[0]).lower() in ("wsl", "wsl.exe") or
                          any(a.replace("\\", "/").endswith("build_linux.sh") for a in cmd))


def parse_tasklist_kb(text):
    """WSL's VM's memory in GB from `tasklist /FO CSV /NH` output ("name","pid","session","n","1,518,992 K"): the
    vmmemWSL line, else a plain vmmem only when it is the one VM process (older WSL names it so; beside vmmemWSL a
    vmmem is another Hyper-V VM's, Docker's or WSA's); None when there is none."""
    found = {}
    for line in text.splitlines():
        fields = [f.strip('"') for f in line.strip().split('","')]
        name = fields[0].strip('"').lower() if fields else ""
        digits = "".join(ch for ch in fields[4] if ch.isdigit()) if len(fields) >= 5 else ""
        if name in ("vmmemwsl", "vmmem") and digits:
            found.setdefault(name, []).append(int(digits) / 2**20)
    if found.get("vmmemwsl"):
        return found["vmmemwsl"][0]
    return found["vmmem"][0] if len(found.get("vmmem", [])) == 1 else None


def wsl_gb():
    """vmmemWSL's working set in GB (Windows; None when WSL's VM isn't running or tasklist won't say)."""
    if not WINDOWS:
        return None
    try:
        out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq vmmem*", "/FO", "CSV", "/NH"], capture_output=True,
                             text=True, timeout=15, creationflags=subprocess.CREATE_NO_WINDOW).stdout
    except (OSError, subprocess.SubprocessError):
        return None
    return parse_tasklist_kb(out)


def wsl_running():
    """Whether a WSL distribution runs now (`wsl --list --running`, which starts nothing): a plain vmmem can be
    another Hyper-V VM's, and wsl.exe -e would start WSL."""
    try:
        r = subprocess.run(["wsl.exe", "--list", "--running", "--quiet"], capture_output=True, timeout=30,
                           creationflags=subprocess.CREATE_NO_WINDOW)
    except (OSError, subprocess.SubprocessError):
        return False
    # (exit 0 too: a build that says 'no running distributions' instead of printing nothing exits non-zero)
    return r.returncode == 0 and bool(r.stdout.decode("utf-16-le", errors="ignore").replace("\x00", "").strip())


def wsl_drop(reason):
    """Drop WSL's page cache and compact its memory, if its VM runs (never starts it), and log what it gave back."""
    if not WINDOWS:
        return
    before = wsl_gb()
    if before is None or not wsl_running():
        return   # (WSL's VM isn't running: nothing to give back, and wsl.exe would start it)
    try:
        subprocess.run(["wsl.exe", "-u", "root", "-e", "sh", "-c", WSL_DROP], capture_output=True, timeout=120,
                       creationflags=subprocess.CREATE_NO_WINDOW)
    except (OSError, subprocess.SubprocessError) as e:
        log_line(f"WSL's caches not dropped ({reason}): {e}")
        return
    _wsl_looked_at[0] = time.time()
    time.sleep(5)   # (the VM hands the freed pages back over a few seconds)
    after = wsl_gb()
    log_line(f"WSL's caches dropped ({reason}): vmmemWSL {before:.1f} -> "
             f"{after if after is not None else 0:.1f} GB, {available_gb():.1f} GB available")


def try_take(kinds, what):
    """One choice per kind (heavy: heavy-1 or heavy-2), all or nothing: the paths held, or None."""
    held = []
    for kind in kinds:
        got = None
        for names in slots_table()[kind]:
            for n in names:
                reap(n)
            paths = []
            for n in names:
                p = claim(n, what)
                if not p:
                    break
                paths.append(p)
            if len(paths) == len(names):
                got = paths
                break
            for p in paths:
                shutil.rmtree(p, ignore_errors=True)
        if got is None:
            for p in held:
                shutil.rmtree(p, ignore_errors=True)
            return None
        held += got
    return held


# ---- the queue: waiters served in the order they came

def queue_dir():
    return os.path.join(locks_dir(), "queue")


def slot_names(kinds):
    """Every slot folder the kinds may take (heavy: heavy-1 and heavy-2)."""
    return sorted({n for kind in kinds for names in slots_table()[kind] for n in names})


def enqueue(kinds, what):
    """A ticket in the queue: named by the time it was made (so they sort in arrival order) and this process; its
    lines are the people's line, the holder line, and the slots it waits for. Returns its path."""
    os.makedirs(queue_dir(), exist_ok=True)
    lane = os.path.basename(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    when = datetime.datetime.now().astimezone().isoformat(timespec="seconds")
    path = os.path.join(queue_dir(), f"{time.time_ns():020d}-{os.getpid()}.txt")
    with open(path, "w", encoding="utf-8") as f:
        f.write(f"{lane} {what} {when}\n{holder_line()}\nslots={'+'.join(slot_names(kinds))}\n")
    return path


def read_ticket(path):
    """(pid, start, first line, slot names) of a ticket; pid None when it cannot be read."""
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            lines = f.read().splitlines()
    except OSError:
        return None, None, "", set()
    pid = start = None
    names = set()
    for line in lines[1:]:
        fields = dict(w.split("=", 1) for w in line.split() if "=" in w)
        try:
            if "pid" in fields:
                pid = int(fields["pid"])
                start = None if fields.get("start", "None") == "None" else int(fields["start"])
        except ValueError:
            pid = None
        if "slots" in fields:
            names = set(fields["slots"].split("+"))
    return pid, start, lines[0] if lines else "", names


def ticket_stale(path):
    """Why a ticket's waiter is gone, or None while it may still wait (as stale() for a slot's holder)."""
    pid, start, _, _ = read_ticket(path)
    if pid is None:
        return None if os.path.exists(path) else "it is gone"
    now = process_start(pid)
    if now == UNKNOWN:
        return None
    if now is None:
        return f"its waiter (pid {pid}) is not running"
    if start is not None and now != start:
        return f"its waiter's pid {pid} now belongs to another process"
    return None


def ahead_of(ticket):
    """The first live ticket older than `ticket` that waits for one of its slots (its first line), or None. Stale
    tickets on the way are removed, under the reaping lock as stale slots are."""
    mine = read_ticket(ticket)[3]
    try:
        names = sorted(n for n in os.listdir(queue_dir()) if n.endswith(".txt"))
    except OSError:
        return None
    for n in names:
        path = os.path.join(queue_dir(), n)
        if os.path.normcase(path) == os.path.normcase(ticket) or n > os.path.basename(ticket):
            return None
        _, _, first, theirs = read_ticket(path)
        if not (mine & theirs):
            continue
        if ticket_stale(path):
            with reap_lock():
                why = ticket_stale(path)
                if why:
                    with contextlib.suppress(OSError):
                        os.remove(path)
                    log_line(f"removed the stale ticket {n}: {why} ({first.strip()})")
                    continue
        return first.strip() or n
    return None


def floor_for(kinds, min_gb):
    """The available memory to wait for: min_gb, capped while LOW_MEMORY (FLOOR_CAPS, live-game counting only alone).
    Never raised."""
    if not LOW_MEMORY:
        return min_gb
    counted = [k for k in kinds if k != "live-game"] or list(kinds)
    return min(min_gb, max(FLOOR_CAPS[k] for k in counted))


def take(kinds, what, min_gb=8.0, poll=5.0):
    """Wait for this waiter's turn (no older live ticket for any of the same slots), the memory and the slots, then
    take them: the paths held. The ticket goes when the slots are taken, or with this process if it is stopped."""
    if LOW_MEMORY and "heavy-both" in kinds:
        # (its 7 GB is above the available memory: its ticket would hold back every heavy job and game build)
        raise SystemExit("slot: heavy-both is refused while LOW_MEMORY (until the owner's reboot): it needs 7 GB, "
                         "and its ticket would hold back every heavy job and game build")
    floor = floor_for(kinds, min_gb)
    if floor != min_gb:
        log_line(f"memory floor {floor:g} GB for {'+'.join(kinds)}, not {min_gb:g} (LOW_MEMORY)")
        min_gb = floor
    ticket = enqueue(kinds, what)
    said, said_at = None, 0.0
    try:
        while True:
            first = ahead_of(ticket)
            avail = available_gb()
            if first is None and avail < min_gb and time.time() - _wsl_looked_at[0] >= WSL_DROP_EVERY_S:
                wsl = wsl_gb()   # (this waiter's turn, short of memory: WSL's VM may hold what Linux no longer uses)
                if wsl is not None and wsl > WSL_DROP_ABOVE_GB:
                    wsl_drop(f"{avail:.1f} GB available, under {min_gb:g}")
                    avail = available_gb()
                else:
                    _wsl_looked_at[0] = time.time()   # (looked: not worth it now; look again in a while)
            if first is None and avail >= min_gb:
                held = try_take(kinds, what)
                if held:
                    return held
            news = f"waiting behind: {first}" if first else f"waiting for the slots and {min_gb:g} GB available"
            if news != said or time.time() - said_at >= WSL_DROP_EVERY_S:   # (a change, or every few minutes)
                wsl = None if first else wsl_gb()
                log_line(news + ("" if first else f" ({avail:.1f} GB now"
                                 + (f"; WSL's VM holds {wsl:.1f} GB" if wsl is not None else "") + ")"))
                said, said_at = news, time.time()
            time.sleep(poll)
    finally:
        with contextlib.suppress(OSError):
            os.remove(ticket)


def kill_with_me():
    """Windows: this process and all it starts in a job object killed when this process ends (its handle closes)."""
    if not WINDOWS:
        return
    k32 = ctypes.windll.kernel32
    k32.CreateJobObjectW.restype = ctypes.c_void_p
    job = k32.CreateJobObjectW(None, None)
    if not job:
        log_line("no job object: the commands could outlive a kill of this runner")
        return

    class LIMITS(ctypes.Structure):   # JOBOBJECT_EXTENDED_LIMIT_INFORMATION (x64 layout)
        _fields_ = [("per_process", ctypes.c_longlong), ("per_job", ctypes.c_longlong), ("flags", ctypes.c_ulong),
                    ("min_ws", ctypes.c_size_t), ("max_ws", ctypes.c_size_t), ("active", ctypes.c_ulong),
                    ("affinity", ctypes.c_size_t), ("priority", ctypes.c_ulong), ("sched", ctypes.c_ulong),
                    ("io", ctypes.c_ulonglong * 6), ("process_memory", ctypes.c_size_t),
                    ("job_memory", ctypes.c_size_t), ("peak_process", ctypes.c_size_t), ("peak_job", ctypes.c_size_t)]
    info = LIMITS()
    info.flags = 0x2000   # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
    if not (k32.SetInformationJobObject(ctypes.c_void_p(job), 9, ctypes.byref(info), ctypes.sizeof(info)) and
            k32.AssignProcessToJobObject(ctypes.c_void_p(job), ctypes.c_void_p(k32.GetCurrentProcess()))):
        log_line("no job object (a parent's job forbids it?): the commands could outlive a kill of this runner")
        return None
    # (the handle stays open for this process's life: closed by the OS at its end, which kills the job)

    def peak_gb():
        """The job's peak committed memory so far (every process it ran), in GB; None if Windows won't say."""
        out = LIMITS()
        ok = k32.QueryInformationJobObject(ctypes.c_void_p(job), 9, ctypes.byref(out), ctypes.sizeof(out), None)
        return out.peak_job / 2**30 if ok else None
    return peak_gb


@contextlib.contextmanager
def held(kinds, what, min_gb=8.0):
    paths = take(kinds, what, min_gb)
    try:
        yield [os.path.basename(p) for p in paths]
    finally:
        for p in paths:
            shutil.rmtree(p, ignore_errors=True)


# ---- the runner

def run(args):
    cmd = args.cmd[1:] if args.cmd and args.cmd[0] == "--" else args.cmd
    commands, cur = [], []
    for w in cmd:
        if w == "---":
            commands.append(cur)
            cur = []
        else:
            cur.append(w)
    commands.append(cur)
    if not commands[0]:
        sys.exit("slot: no command")
    env = dict(os.environ)
    for kv in args.env:
        k, _, v = kv.partition("=")
        env[k] = v
    kinds = args.slot.split("+")
    for k in kinds:
        if k not in SLOTS:
            sys.exit(f"slot: unknown slot {k!r} (one of {', '.join(SLOTS)})")
    peak_gb = kill_with_me()   # (Windows: the job's peak memory, logged at each exit for the floors' sake)
    rc = 0
    with held(kinds, args.what, args.min_gb) as names:
        names = "+".join(names)
        flags = subprocess.CREATE_NO_WINDOW if WINDOWS else 0
        with open(args.log, "a", encoding="utf-8") as log:
            log.write(f"[{names}] {args.what} from {time.strftime('%H:%M:%S')}\n")
            for c in commands:
                start = time.time()
                log.write(f"$ {' '.join(c)}\n")
                log.flush()
                rc = subprocess.run(c, cwd=args.cwd, env=env, stdout=log, stderr=subprocess.STDOUT,
                                    creationflags=flags).returncode
                peak = peak_gb() if peak_gb else None
                log.write(f"[exit {rc} after {time.time() - start:.0f} s, {time.strftime('%H:%M:%S')}"
                          + (f", peak {peak:.2f} GB so far" if peak is not None else "") + "]\n")
                log.flush()
                if rc:
                    break
        if any(is_wsl_command(c) for c in commands):
            wsl_drop(f"after {args.what}")   # (inside the slots: the next job starts with the memory back)
    print(f"{args.what}: exit {rc}, slot {names} released")
    return rc


def self_test():
    """The stale checks on a temporary lock folder: a live holder kept, a dead one and a reused PID removed, a slot
    without a holder line kept, and a take that heals."""
    global LOW_MEMORY
    os.environ["GT4_LOCKS"] = tempfile.mkdtemp(prefix="gt4-slot-test-")
    failures = []
    low_memory, LOW_MEMORY = LOW_MEMORY, False   # (the checks below first as without it; then with it)

    def expect(ok, text):
        print(("ok   " if ok else "FAIL ") + text)
        if not ok:
            failures.append(text)

    def make(name, second):
        os.mkdir(os.path.join(locks_dir(), name))
        with open(os.path.join(locks_dir(), name, "owner.txt"), "w") as f:
            f.write(f"test {name}\n{second}\n")

    try:
        make("heavy-1", holder_line())
        expect(not reap("heavy-1") and os.path.isdir(os.path.join(locks_dir(), "heavy-1")), "a live holder is kept")
        child = subprocess.Popen([sys.executable, "-c", "pass"])
        child.wait()
        make("heavy-2", f"pid={child.pid} start=1")
        expect(stale(os.path.join(locks_dir(), "heavy-2")) is not None, "a holder that has exited is stale")
        expect(reap("heavy-2") and not os.path.exists(os.path.join(locks_dir(), "heavy-2")), "its slot is removed")
        make("live-game", f"pid={os.getpid()} start={(process_start(os.getpid()) or 0) + 1}")
        expect("another process" in (stale(os.path.join(locks_dir(), "live-game")) or ""), "a reused PID is stale")
        reap("live-game")
        if WINDOWS and process_start(4) == UNKNOWN:   # (the System process: refused to a user, opened by an admin)
            make("live-game", "pid=4 start=1")
            expect(stale(os.path.join(locks_dir(), "live-game")) is None, "a holder that cannot be opened is kept")
            shutil.rmtree(os.path.join(locks_dir(), "live-game"))
        make("gt4-decomp-release", "")
        expect(stale(os.path.join(locks_dir(), "gt4-decomp-release")) is None, "a slot without a holder line is kept")
        shutil.rmtree(os.path.join(locks_dir(), "gt4-decomp-release"))
        make("heavy-2", f"pid={child.pid} start=1")   # heavy-1 is held (live): a take must heal heavy-2
        got = try_take(["heavy"], "self-test")
        expect(bool(got) and os.path.basename(got[0]) == "heavy-2", "a take heals a stale slot and gets it")
        expect(try_take(["heavy"], "self-test") is None, "with both held by live holders, nothing is taken")
        # a game build takes a heavy slot too: at most two of the big jobs at once
        expect(try_take(["game"], "self-test") is None and
               not os.path.exists(os.path.join(locks_dir(), "gt4-decomp-release")),
               "no game build beside two heavy jobs, and nothing kept from the try")
        shutil.rmtree(got[0])   # heavy-2 free again
        game = try_take(["game"], "self-test")
        expect(bool(game) and sorted(os.path.basename(p) for p in game) == ["gt4-decomp-release", "heavy-2"],
               "a game build takes the free heavy slot with its own")
        expect(try_take(["heavy"], "self-test") is None, "and no second heavy job runs beside it")
        for p in game:
            shutil.rmtree(p)
        for n in ("heavy-1",):
            shutil.rmtree(os.path.join(locks_dir(), n))

        # the queue: an older live waiter for the same slots goes first; other slots' and gone waiters' don't count
        os.makedirs(queue_dir(), exist_ok=True)
        waiter = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])
        try:
            def ticket(age, pid, start, slots):
                path = os.path.join(queue_dir(), f"{time.time_ns() - age:020d}-{pid}.txt")
                with open(path, "w") as f:
                    f.write(f"test ticket {age}\npid={pid} start={start}\nslots={slots}\n")
                return path
            mine = enqueue(["heavy"], "self-test")
            other = ticket(10**9, waiter.pid, process_start(waiter.pid), "live-game")
            expect(ahead_of(mine) is None, "an older waiter for other slots doesn't hold a waiter back")
            gone = ticket(2 * 10**9, child.pid, 1, "heavy-1+heavy-2")
            expect(ahead_of(mine) is None and not os.path.exists(gone),
                   "an older ticket whose waiter is gone is removed")
            older = ticket(3 * 10**9, waiter.pid, process_start(waiter.pid), "heavy-1+heavy-2")
            expect(ahead_of(mine) == "test ticket 3000000000", "an older live waiter for the same slots goes first")
            expect(ahead_of(older) is None, "and that waiter has no one ahead of it")
            for p in (mine, other, older):
                os.remove(p)
            got = take(["heavy"], "self-test", min_gb=0)
            expect(bool(got) and not os.listdir(queue_dir()), "a take with no one ahead gets a slot; its ticket goes")
            for p in got:
                shutil.rmtree(p)
        finally:
            waiter.kill()
            waiter.wait()

        # low memory: the floors capped per kind, one heavy job at a time and never beside a game build
        expect(floor_for(["heavy"], 7) == 7 and floor_for(["game"], 7) == 7, "without LOW_MEMORY the floors stand")
        # WSL's memory: which jobs are WSL's, and vmmemWSL's size read from tasklist's CSV
        expect(is_wsl_command(["wsl.exe", "-e", "bash", "-c", "make"]) and is_wsl_command(["C:\\x\\WSL.EXE"]) and
               is_wsl_command(["bash", "tools/build_linux.sh", "aarch64"]) and
               is_wsl_command(["wsl", "-e", "bash", "tools\\build_linux.sh"]) and
               not is_wsl_command(["cmd", "/c", "tools\\build_windows.cmd", "Release"]) and not is_wsl_command([]),
               "WSL jobs: wsl(.exe), or tools/build_linux.sh; not the Windows build")
        sizes = (parse_tasklist_kb('"vmmemWSL","13496","Services","0","1,518,992 K"\n'),
                 parse_tasklist_kb('"vmmemWSL","13496","Services","0","19.315.472 K"'),
                 parse_tasklist_kb("INFO: No tasks are running which match the specified criteria.\n"),
                 parse_tasklist_kb('"vmmem","4410","Services","0","2,097,152 K"'),
                 parse_tasklist_kb('"vmmem","4410","Services","0","9,437,184 K"\n'
                                   '"vmmemWSL","13496","Services","0","2,097,152 K"'),
                 parse_tasklist_kb('"vmmem","4410","Services","0","1,048,576 K"\n'
                                   '"vmmem","4411","Services","0","2,097,152 K"'))
        expect(abs(sizes[0] - 1518992 / 2**20) < 1e-9 and abs(sizes[1] - 19315472 / 2**20) < 1e-9 and sizes[2] is None
               and sizes[3] == 2.0 and sizes[4] == 2.0 and sizes[5] is None,
               "the VM's size from tasklist (either digit grouping; a lone vmmem; vmmemWSL beside another VM's vmmem; "
               "None when absent or when two vmmem can't be told apart)")
        LOW_MEMORY = True
        expect(floor_for(["game"], 7) == 5.5 and floor_for(["live-game"], 7) == 5.5 and floor_for(["heavy"], 7) == 4.5
               and floor_for(["heavy", "live-game"], 7) == 4.5 and floor_for(["heavy-both"], 7) == 7,
               "LOW_MEMORY caps: game and live-game 5.5, heavy 4.5 (live-game beside it too), heavy-both 7")
        expect(floor_for(["game"], 4) == 4 and floor_for(["heavy"], 3) == 3, "and never raises a lower --min-gb")
        try:
            take(["heavy-both"], "self-test", min_gb=0)
            refused = False
        except SystemExit:
            refused = True
        expect(refused and not os.listdir(queue_dir()),
               "heavy-both is refused, with no ticket left to block the queue")
        game = try_take(["game"], "self-test")
        expect(bool(game) and try_take(["heavy"], "self-test") is None, "a heavy job waits while a game build runs")
        for p in game:
            shutil.rmtree(p)
        heavy = try_take(["heavy"], "self-test")
        expect(bool(heavy) and sorted(os.path.basename(p) for p in heavy) == ["heavy-1", "low-memory"] and
               try_take(["heavy"], "self-test") is None and try_take(["game"], "self-test") is None,
               "one heavy job at a time (heavy-1 only), and no game build beside it")
    finally:
        LOW_MEMORY = low_memory
        shutil.rmtree(locks_dir(), ignore_errors=True)
    print(f"{len(failures)} failed")
    return 1 if failures else 0


def main():
    if "--self-test" in sys.argv[1:2]:
        sys.exit(self_test())
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--slot", required=True, help="game, heavy, heavy-both, live-game, or several joined by +")
    ap.add_argument("--what", required=True, help="what the slot is for (owner.txt's first line)")
    ap.add_argument("--log", required=True, help="the commands' output, appended")
    ap.add_argument("--cwd")
    ap.add_argument("--env", action="append", default=[], help="K=V for the commands")
    ap.add_argument("--min-gb", type=float, default=8.0,
                    help="the available memory (free plus standby) to wait for (default 8)")
    ap.add_argument("cmd", nargs=argparse.REMAINDER)
    sys.exit(run(ap.parse_args()))


if __name__ == "__main__":
    main()
