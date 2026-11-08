#!/usr/bin/env python3
"""Run commands while holding machine-wide slots (lock folders), without a shell.

    py -I tools/slot.py --slot SLOT[+SLOT...] --what TEXT --log FILE [--cwd DIR] [--env K=V ...] [--min-gb N]
                        -- CMD ARGS [--- CMD ARGS ...]

SLOT: game (gt4-decomp-release: game builds), heavy (heavy-1 or heavy-2: host builds, live games, replays),
heavy-both (both: a replay over 2 GB), live-game (the console's focus: a windowed game or PCSX2);
docs/modern/CAPTURES.md lists them. A slot is a folder under GT4_LOCKS (default: ~/locks), made with mkdir (atomic);
owner.txt in it says who holds it, its first line for people, its second "pid=N start=T" (the holder and its start
time) for the waiters.

Waits until N GB are available (default 8) and every slot asked for is free, takes them together (a partial hold is
given back, never kept while waiting), runs each command in turn (stops at the first failure), and removes the slots
on exit.

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
SLOTS = {"game": [["gt4-decomp-release"]], "heavy": [["heavy-1"], ["heavy-2"]], "heavy-both": [["heavy-1", "heavy-2"]],
         "live-game": [["live-game"]]}


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


def try_take(kinds, what):
    """One choice per kind (heavy: heavy-1 or heavy-2), all or nothing: the paths held, or None."""
    held = []
    for kind in kinds:
        got = None
        for names in SLOTS[kind]:
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


def take(kinds, what, min_gb=8.0, poll=30.0):
    while True:
        if available_gb() >= min_gb:
            held = try_take(kinds, what)
            if held:
                return held
        time.sleep(poll)


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
    # (the handle stays open for this process's life: closed by the OS at its end, which kills the job)


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
    kill_with_me()
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
                log.write(f"[exit {rc} after {time.time() - start:.0f} s, {time.strftime('%H:%M:%S')}]\n")
                log.flush()
                if rc:
                    break
    print(f"{args.what}: exit {rc}, slot {names} released")
    return rc


def self_test():
    """The stale checks on a temporary lock folder: a live holder kept, a dead one and a reused PID removed, a slot
    without a holder line kept, and a take that heals."""
    os.environ["GT4_LOCKS"] = tempfile.mkdtemp(prefix="gt4-slot-test-")
    failures = []

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
    finally:
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
