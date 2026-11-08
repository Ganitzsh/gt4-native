"""Proves that a commit range changes only comments or docs (PLAN.md rule 9).

    py tools/comment_only.py [BASE..HEAD]     (default origin/main..HEAD: the changes since the merge base)
    py tools/comment_only.py --self-test

Each changed file gets a verdict, and the exit code is 0 only if every file passes:
- C, C++ and GLSL (.c .cpp .h .hpp .inl .glsl .vert .frag .comp): the old and new versions with their comments
  stripped by the compiler (g++ -fpreprocessed -E -P) and whitespace (CRs included) collapsed must be equal.
  Reading `git diff -w` line by line is not enough: a // comment ending in a backslash swallows the line below it,
  and a /* and a */ on two changed lines comment out the unchanged code between them; the compiler sees both.
  Backslash-newlines are joined first (-fpreprocessed skips that phase, a real compile does not), then every
  directive line's # is masked, so g++ keeps directives as text (it drops GLSL's #version and #extension
  otherwise) and a changed #define or #version counts as code.
- tools/eeref/known.txt: only its '#' lines and blank lines are comments; every entry, reason included, is data.
- *.md: docs, passes (added, changed or deleted).
- Anything else, and any added or deleted file that is not docs: not provable, fails.
Without g++ (PATH, C:/msys64's mingw32/mingw64/ucrt64, C:/mingw64, C:/Strawberry/c), a source file fails: the
tool never passes unproven.
"""
import os, re, shutil, subprocess, sys

SOURCE = {".c", ".cpp", ".h", ".hpp", ".inl", ".glsl", ".vert", ".frag", ".comp"}
KNOWN = "tools/eeref/known.txt"
MASK = b"__gt4_directive__ "   # stands for a directive's #: g++ -fpreprocessed keeps it as an ordinary token
DIRECTIVE = re.compile(rb"^([ \t]*)#", re.M)
SPLICE = re.compile(rb"\\[ \t\r]*\n")   # a backslash-newline (GCC also takes spaces or a CR between): lines joined


def find_gxx():
    gxx = shutil.which("g++")
    if gxx:
        return gxx
    candidates = [os.path.join("C:/msys64", sub, "bin", "g++.exe") for sub in ("mingw32", "mingw64", "ucrt64")]
    candidates += ["C:/mingw64/bin/g++.exe", "C:/Strawberry/c/bin/g++.exe"]   # where CI's Windows image keeps one
    for p in candidates:
        if os.path.exists(p):
            return p
    return None


def stripped(gxx, data):
    """data's code with comments removed and whitespace collapsed, or (None, why)."""
    env = dict(os.environ)
    env["PATH"] = os.path.dirname(gxx) + os.pathsep + env.get("PATH", "")   # cc1plus's DLLs on Windows
    # -fpreprocessed takes its input as past translation phase 2, so the lines are joined here first, as a real
    # compile joins them before it removes comments; then the directives are masked
    code = DIRECTIVE.sub(rb"\1" + MASK, SPLICE.sub(b"", data))
    p = subprocess.run([gxx, "-x", "c++", "-fpreprocessed", "-E", "-P", "-"], input=code, capture_output=True, env=env)
    if p.returncode:
        err = p.stderr.decode("utf-8", "replace").strip().splitlines()
        return None, "g++ failed: " + (err[0] if err else f"exit {p.returncode}")
    return b" ".join(p.stdout.split()), None


def known_entries(data):
    lines = data.decode("utf-8", "replace").splitlines()
    return [l.rstrip() for l in lines if l.strip() and not l.startswith("#")]


def verdict(path, old, new, gxx):
    """(passes, why) for one file; old or new is None when the file is added or deleted."""
    ext = os.path.splitext(path)[1].lower()
    if ext == ".md":
        return True, "docs"
    if old is None or new is None:
        return False, ("added" if old is None else "deleted") + ", not docs"
    if path == KNOWN:
        if known_entries(old) == known_entries(new):
            return True, "only '#' or blank lines changed"
        return False, "an entry changed (entries are data; lane U reviews them)"
    if ext in SOURCE:
        if not gxx:
            return False, "not provable: g++ not found"
        a, why = stripped(gxx, old)
        if a is None:
            return False, "old version: " + why
        b, why = stripped(gxx, new)
        if b is None:
            return False, "new version: " + why
        return (True, "comments only") if a == b else (False, "code changed (comments stripped, the code differs)")
    return False, "not provable (not C, C++, GLSL, known.txt or docs)"


def git(*args):
    p = subprocess.run(["git", *args], capture_output=True)
    if p.returncode:
        sys.exit("git " + " ".join(args) + ": " + p.stderr.decode("utf-8", "replace").strip())
    return p.stdout


def blob(rev, path):
    p = subprocess.run(["git", "show", f"{rev}:{path}"], capture_output=True)
    return p.stdout if p.returncode == 0 else None


def check_range(spec):
    base, _, head = spec.partition("..")
    head = head or "HEAD"
    base = git("merge-base", base, head).decode().strip()
    gxx = find_gxx()
    rows = git("diff", "--name-status", "--no-renames", "-z", base, head).split(b"\0")
    ok, count = True, 0
    for status, path in zip(rows[0::2], rows[1::2]):
        status, path = status.decode(), path.decode("utf-8", "replace")
        if not status:
            continue
        count += 1
        old = None if status == "A" else blob(base, path)
        new = None if status == "D" else blob(head, path)
        passes, why = verdict(path, old, new, gxx)
        ok &= passes
        print(f"{'pass' if passes else 'FAIL'}  {path}: {why}")
    if not count:
        print("no changed files")
    print("comment-only: yes" if ok else "comment-only: NO (run the gates)")
    return 0 if ok else 1


def self_test():
    gxx = find_gxx()
    if not gxx:
        print("comment_only self-test: skipped, no g++")
        return 77
    base = b"int f(int a) {\n    // add one\n    a += 1;\n    return a;\n}\n"
    cases = [   # (name, path, old, new, expected)
        ("comment text changed", "f.cpp", base, base.replace(b"add one", b"one more"), True),
        ("comment moved to its own line", "f.cpp", base,
         base.replace(b"    return a;", b"    /* then */\n    return a;"), True),
        ("CRLF and indentation only", "f.cpp", base, base.replace(b"\n", b"\r\n").replace(b"    ", b"\t"), True),
        ("hole 1: a // comment ending in a backslash swallows the next line", "f.cpp", base,
         base.replace(b"// add one", b"// add one \\"), False),
        ("hole 1 with CRLF and a space after the backslash", "f.cpp", base.replace(b"\n", b"\r\n"),
         base.replace(b"\n", b"\r\n").replace(b"// add one", b"// add one \\ "), False),
        ("a backslash joining two comment lines", "f.cpp", base,
         base.replace(b"// add one", b"// add \\\n    // one"), True),
        ("hole 2: /* and */ on two changed lines comment out the code between", "f.cpp", base,
         base.replace(b"// add one", b"/* add one").replace(b"    return a;", b"    */ return a;"), False),
        ("code changed", "f.cpp", base, base.replace(b"a += 1", b"a += 2"), False),
        ("a // inside a string is code", "f.cpp", b'const char *s = "a";\n', b'const char *s = "a // b";\n', False),
        ("a changed #define is code", "f.h", b"#define N 1 // n\n", b"#define N 2 // n\n", False),
        ("a #define's comment", "f.h", b"#define N 1 // n\n", b"#define N 1 // the count\n", True),
        ("GLSL comment", "s.frag", b"#version 450\n// c\nvoid main() {}\n",
         b"#version 450\n// d\nvoid main() {}\n", True),
        ("GLSL #version changed", "s.frag", b"#version 450\nvoid main() {}\n",
         b"#version 460\nvoid main() {}\n", False),
        ("unterminated comment", "f.cpp", base, base.replace(b"// add one", b"/* add one"), False),
        ("known.txt # line", KNOWN, b"# a\n0049DA60  why\n", b"# b\n\n0049DA60  why\n", True),
        ("known.txt reason", KNOWN, b"# a\n0049DA60  why\n", b"# a\n0049DA60  because\n", False),
        ("known.txt entry added", KNOWN, b"# a\n", b"# a\n0049DA60  why\n", False),
        ("docs", "docs/x.md", b"a\n", b"b\n", True),
        ("docs added", "docs/x.md", None, b"b\n", True),
        ("source added", "f.cpp", None, base, False),
        ("source deleted", "f.cpp", base, None, False),
        ("other file", "tools/x.py", b"a = 1\n", b"a = 1  # c\n", False),
    ]
    failed = 0
    for name, path, old, new, expected in cases:
        got, why = verdict(path, old, new, gxx)
        if got != expected:
            failed += 1
            print(f"FAIL  {name}: got {got} ({why}), expected {expected}")
    total = len(cases) + 2
    if verdict("f.cpp", base, base, None)[0]:
        failed += 1
        print("FAIL  without g++ a source file passed")
    failed += git_case(base)
    print(f"comment_only self-test: {total - failed} of {total} cases right")
    return 1 if failed else 0


def git_case(base):
    """A range through git in a temporary repository: a comment edit passes; an added source then fails it."""
    import tempfile
    here = os.getcwd()
    with tempfile.TemporaryDirectory() as tmp:
        try:
            os.chdir(tmp)
            g = ["git", "-c", "user.name=t", "-c", "user.email=t@t", "-c", "core.autocrlf=false"]
            run = lambda *a: subprocess.run(g + list(a), capture_output=True, check=True)
            run("init", "-q")
            open("f.cpp", "wb").write(base)
            open("old.cpp", "wb").write(base)
            run("add", "."); run("commit", "-qm", "a")
            open("f.cpp", "wb").write(base.replace(b"add one", b"one more"))
            os.remove("old.cpp")
            open("notes.md", "wb").write(b"x\n")
            run("add", "-A"); run("commit", "-qm", "b")
            first = check_range("HEAD~1..HEAD")   # f.cpp passes, notes.md passes, old.cpp deleted: fails
            run("checkout", "-q", "HEAD~1", "--", "old.cpp")
            run("add", "-A"); run("commit", "-qm", "c")
            second = check_range("HEAD~2..HEAD")  # old.cpp back: f.cpp's comment and notes.md overall, passes
        except (subprocess.CalledProcessError, OSError) as e:
            print(f"FAIL  git case: {e}")
            return 1
        finally:
            os.chdir(here)
    if (first, second) != (1, 0):
        print(f"FAIL  git case: exit codes {first}, {second}; expected 1, 0")
        return 1
    return 0


def main():
    if sys.argv[1:2] == ["--self-test"]:
        sys.exit(self_test())
    if len(sys.argv) > 2 or (len(sys.argv) == 2 and (".." not in sys.argv[1] or sys.argv[1].startswith("-"))):
        sys.exit(__doc__)
    sys.exit(check_range(sys.argv[1] if len(sys.argv) == 2 else "origin/main..HEAD"))


if __name__ == "__main__":
    main()
