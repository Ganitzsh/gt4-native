#!/usr/bin/env python3
"""Replays every call recorded in a directory (GT4_RECORD) on the original code (eeref) and summarises.

    py tools/eeref/replay.py --decomp C:\\path\\to\\gt4-decomp DIR [--all-lines] [--detail ADDR_N]

Each function's parameters and return are read from the source the native link uses for it (game_src/, the
matched source, else its draft; tools/abi_lint.py parses them), so floats reach the EE's float registers and
the return value is compared. Prints one line per call, differing ones first (docs/DIFFERENTIAL_TESTING.md).
"""
import argparse
import glob
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import abi_lint  # noqa: E402


def drafts(game):
    out = {}
    path = os.path.join(game, 'draft_natives.txt')
    if os.path.exists(path):
        for line in open(path, encoding='utf-8', errors='replace'):
            parts = line.split()
            if len(parts) >= 2 and not line.startswith('#'):
                out[parts[0].upper()] = parts[1].replace('\\', '/')
    return out


def definition(addr, decomp, draft_paths):
    """(kinds, ret, role) of the definition the native link compiles for addr, or None."""
    cands = []
    for ext in ('.c', '.cpp'):
        p = os.path.join(ROOT, 'game_src', 'func_%s%s' % (addr, ext))
        if os.path.exists(p):
            cands.append((p, 'game_src'))
    out = subprocess.run(['git', '-C', decomp, 'grep', '-l', 'func_%s' % addr, '--', 'src'], capture_output=True,
                         text=True).stdout.split()
    cands += [(os.path.join(decomp, p), 'src') for p in out]
    if addr in draft_paths:
        cands.append((os.path.join(decomp, draft_paths[addr]), 'draft'))
    for path, role in cands:
        try:
            text = abi_lint.strip_comments(open(path, encoding='utf-8', errors='replace').read())
        except OSError:
            continue
        for a, kinds, is_def, _line, _used, ret in abi_lint.scan(path, text):
            if a == addr and is_def and kinds is not None:
                return kinds, ret, role
    return None


def consistent(path):
    """Whether a recorded file's length is what its header says (recordings made before record.cpp copied the
    changed pages in one pass can count one page fewer than they hold, when another thread wrote meanwhile)."""
    import struct
    if not os.path.exists(path):
        return True
    data = open(path, 'rb').read(124)
    kind, = struct.unpack_from('<I', data, 20)
    ram_size, spr_size, vu_size, vu0_size, _base, stack_size, pages = struct.unpack_from('<7I', data, 96)
    ram = ram_size if kind == 0 else pages * (4 + 0x1000)
    return os.path.getsize(path) == 124 + vu0_size + ram + spr_size + vu_size + stack_size


def expand(pre, ram=None):
    """A GT4_RECORD_DELTA entry (kind 2: the RAM pages that differ from the entry recorded before it, the first
    from the directory's base.ram) as a full entry file in the temp directory (the caller deletes it); other
    files unchanged (None). ram: the running image of a walk in order.txt order, updated in place."""
    import struct, tempfile, shutil
    data = open(pre, 'rb').read()
    if struct.unpack_from('<I', data, 20)[0] != 2:
        return None
    ram_size, = struct.unpack_from('<I', data, 96)
    vu0_size, = struct.unpack_from('<I', data, 108)
    pages, = struct.unpack_from('<I', data, 120)
    if ram is None:
        ram = bytearray(open(os.path.join(os.path.dirname(pre), 'base.ram'), 'rb').read()[:ram_size])
    off = 124 + vu0_size
    for _ in range(pages):
        a, = struct.unpack_from('<I', data, off)
        ram[a:a + 0x1000] = data[off + 4:off + 4 + 0x1000]
        off += 4 + 0x1000
    head = bytearray(data[:124])
    struct.pack_into('<I', head, 20, 0)
    struct.pack_into('<I', head, 120, 0)
    tmp = tempfile.mkdtemp()   # eeref reads ADDR_N.post next to ADDR_N.pre
    path = os.path.join(tmp, os.path.basename(pre))
    with open(path, 'wb') as f:
        f.write(head + data[124:124 + vu0_size] + ram + data[off:])
    post = pre[:-4] + '.post'
    if os.path.exists(post):
        shutil.copy(post, path[:-4] + '.post')
    return path


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--decomp', required=True)
    ap.add_argument('--game', default=os.path.join(ROOT, 'build', 'game'))
    ap.add_argument('--eeref', default=os.path.join(ROOT, 'build', 'eeref', 'eeref.exe'))
    ap.add_argument('dir')
    ap.add_argument('--detail', help='ADDR_N: print that call\'s differing words')
    ap.add_argument('--max', default='40')
    ap.add_argument('--timeout', type=float, default=60, help='seconds per call before it counts as STOPPED')
    a = ap.parse_args()
    elf = os.path.join(a.decomp, 'orig', 'SCUS-97328', 'CORE.GT4.elf')
    syms = os.path.join(a.decomp, 'config', 'symbol_addrs.txt')
    draft_paths = drafts(a.game)
    known = {l.split()[0].upper() for l in open(os.path.join(ROOT, 'tools', 'eeref', 'known.txt'))
             if l.strip() and not l.startswith('#')}
    pres = sorted(glob.glob(os.path.join(a.dir, '*.pre')))
    order = os.path.join(a.dir, 'order.txt')
    ram = None
    if os.path.exists(order):   # GT4_RECORD_DELTA: each entry's pages are relative to the one before it
        pres = [os.path.join(a.dir, n.strip()) for n in open(order) if n.strip()]
        ram = bytearray(open(os.path.join(a.dir, 'base.ram'), 'rb').read())
    want = lambda p: not a.detail or os.path.basename(p).upper().startswith(a.detail.upper())
    if ram is None:
        pres = [p for p in pres if want(p)]
    rows = []
    for pre in pres:
        if not want(pre):   # a delta chain: its pages still move the running image on
            shutil.rmtree(os.path.dirname(expand(pre, ram)))
            continue
        addr = os.path.basename(pre).split('_')[0].upper()
        d = definition(addr, a.decomp, draft_paths)
        if not consistent(pre[:-4] + '.post') or not consistent(pre):
            if ram is not None:
                shutil.rmtree(os.path.dirname(expand(pre, ram)))
            rows.append((True, os.path.basename(pre)[:-4], d[2] if d else '?', 'CORRUPT', 'file length disagrees with its header'))
            continue
        full = expand(pre, ram)
        cmd = [a.eeref, full or pre, '--elf', elf, '--syms', syms, '--max', a.max if a.detail else '0']
        role = '?'
        if d:
            kinds, ret, role = d
            kinds = [k if k in 'ifwq' else 'i' for k in kinds if k != '...']
            cmd += ['--args', ','.join(kinds) or 'i', '--ret', {'s': 'v'}.get(ret, ret)]
        try:   # a wait on hardware state the reference does not model spins until eeref's budget runs out
            r = subprocess.run(cmd, capture_output=True, text=True, timeout=a.timeout)
        except subprocess.TimeoutExpired:
            r = subprocess.CompletedProcess(cmd, 1, 'did NOT return (timed out after %ss)' % a.timeout, '')
        if full:
            shutil.rmtree(os.path.dirname(full))
        text = (r.stdout + r.stderr).strip()
        if a.detail:
            print(text)
            continue
        m = re.search(r'(\d+) words changed alike, (\d+) within float rounding, (\d+) differ', text)
        near = re.search(r'; (\d+) within 1e-3', text)
        verdict = {0: 'agree', 4: 'close'}.get(r.returncode, 'DIFFER')   # close: float drift within 1e-3 only
        if verdict == 'DIFFER' and addr in known:   # understood differences (tools/eeref/known.txt)
            verdict = 'known'
        if 'did NOT return' in text:
            verdict = 'STOPPED'
        summary = ('alike %s, rounding %s, differ %s' % m.groups()) if m else text.splitlines()[-1][:80]
        if near and near.group(1) != '0':
            summary += ' (%s within 1e-3)' % near.group(1)
        if 'RETURN DIFFERS' in text:
            summary += ', return differs'
        why = re.search(r'stopped at ([0-9A-F]{8}): (.*)', text)
        if why:
            summary += ' [%s at %s%s]' % (why.group(2), why.group(1),
                                          ''.join(' %s' % s for s in re.findall(r'syscall (-?\d+) at', text)[:1]))
        rows.append((verdict not in ('agree', 'close', 'known'), os.path.basename(pre)[:-4], role, verdict, summary))
        sys.stderr.write('%d %s %s\n' % (len(rows), os.path.basename(pre)[:-4], verdict))
        sys.stderr.flush()
    for _bad, name, role, verdict, summary in sorted(rows, key=lambda r: (not r[0], r[1])):
        print('%-14s %-8s %-7s %s' % (name, role, verdict, summary))


if __name__ == '__main__':
    main()
