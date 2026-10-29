#!/usr/bin/env python3
"""Which VU0 registers each function of the native build uses as state shared with other functions
(docs/NATIVE_ONLY.md, layer 3: the game keeps matrices and constants in VU0 registers across calls).

    py tools/vu0_state_map.py --decomp C:\\path\\to\\gt4-decomp [--game build/game] [--elf CORE.GT4.elf]

For every source the native link compiles (game_src/, the matched source, else the draft it stages) whose asm
uses VU0, the VF/VI registers it reads before writing them (inputs: state left by another function) and the ones
it writes (outputs), following vcallms into the micro image (tools/vu0_disasm.py decodes it). Then the clusters:
functions linked by a register one writes and another reads. Registers a function reads only after writing are
its temporaries.
"""
import argparse
import collections
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'game_toolchain'))
import game_cc  # noqa: E402
import vu0_disasm  # noqa: E402

REG = re.compile(r'\$?(vf|vi)(\d+)([xyzw]{0,4})')


def program_regs(img, addr):
    """(reads, writes) of the microprogram at addr, in execution order; follows BAL/JR, ends at E + 1."""
    reads, writes = set(), set()
    pc, ret, steps, end = addr, None, 0, False
    while steps < 2000:
        steps += 1
        lo = int.from_bytes(img[pc:pc + 4], 'little')
        up = int.from_bytes(img[pc + 4:pc + 8], 'little')
        texts = [vu0_disasm.upper(up & 0x7FFFFFFF)]
        if not up & 0x80000000:
            texts.append(vu0_disasm.lower(lo, pc))
        for t in texts:
            name, _, ops = t.partition(' ')
            regs = REG.findall(ops)
            if not regs:
                continue
            dest_first = not name.startswith(('sq', 'isw', 'ib', 'b', 'j', 'div', 'sqrt', 'rsqrt', 'mtir', 'clipw',
                                               'rinit', 'rxor'))
            for k, (kind, n, _l) in enumerate(regs):
                reg = '%s%s' % (kind, n)
                if n == '0':
                    continue
                if k == 0 and dest_first and not name.startswith(('lqi', 'lqd', 'sqi', 'sqd')):
                    if reg not in writes:
                        writes.add(reg)
                    continue
                if reg not in writes:
                    reads.add(reg)
                if name.startswith(('lqi', 'lqd')) and k == 0:
                    writes.add(reg)
            if name.startswith(('bal', 'jalr')):
                ret = pc + 16
        if end:
            break
        if up & 0x40000000:
            end = True
        lower_text = texts[-1]
        m = re.match(r'(b|bal vi\d+,) (0x[0-9A-F]+)', lower_text)
        if m and not (up & 0x80000000):
            pc_next = int(m.group(2), 16)
            # the delay slot runs first
            delay = pc + 8
            lo2 = int.from_bytes(img[delay:delay + 4], 'little')
            up2 = int.from_bytes(img[delay + 4:delay + 8], 'little')
            for t in (vu0_disasm.upper(up2 & 0x7FFFFFFF), vu0_disasm.lower(lo2, delay)):
                for kind, n, _l in REG.findall(t.partition(' ')[2]):
                    if n != '0' and '%s%s' % (kind, n) not in writes:
                        reads.add('%s%s' % (kind, n))
            pc = pc_next
            continue
        if lower_text.startswith('jr') and ret is not None:
            pc, ret = ret, None
            continue
        pc += 8
    return reads, writes


def source_regs(path, img):
    data = open(path, 'rb').read()
    reads, writes = set(), set()
    calls = []
    for m in game_cc.ASM_START.finditer(data):
        end = game_cc._close_paren(data, m.end())
        if end is None:
            continue
        body = data[m.end():end - 1].decode('latin-1')
        text = ''.join(re.findall(r'"((?:[^"\\]|\\.)*)"', body.split(':')[0]))
        for insn in re.split(r'\\n|;', text):
            insn = insn.strip()
            if not insn:
                continue
            name, _, ops = insn.partition(' ')
            cm = re.match(r'vcallms\s+(\S+)', insn)
            if cm:
                addr = int(cm.group(1), 0)
                calls.append(addr)
                r, w = program_regs(img, addr)
                reads |= {x for x in r if x not in writes}
                writes |= w
                continue
            regs = REG.findall(ops)
            for k, (kind, n, _l) in enumerate(regs):
                reg = '%s%s' % (kind, n)
                if n == '0':
                    continue
                acc_dest = ops.strip().upper().startswith('ACC')   # vmulax ACC, $vf24, $vf1x: every vf is read
                to = name.startswith(('lqc2', 'qmtc2', 'ctc2')) or (name.startswith('v') and k == 0 and not acc_dest and
                                                                     not name.startswith(('vclip', 'vdiv', 'vsqrt', 'vrsqrt',
                                                                                          'vnop', 'vwaitq', 'vcallms')))
                if to:
                    writes.add(reg)
                elif reg not in writes:
                    reads.add(reg)
            if name.startswith(('vopmula', 'vmula', 'vmadda', 'vadda', 'vsuba', 'vmsuba')):
                writes.add('acc')
            elif 'acc' not in writes and re.match(r'vm(add|sub)[xyzwqi]?\.', name):
                reads.add('acc')
    return reads, writes, calls


def native_sources(decomp, game):
    out = {}
    lst = os.path.join(game, 'draft_natives.txt')
    if os.path.exists(lst):
        for line in open(lst, encoding='utf-8', errors='replace'):
            p = line.split()
            if len(p) >= 2 and not line.startswith('#'):
                out[p[0].upper()] = os.path.join(decomp, p[1].replace('\\', '/'))
    files = subprocess.run(['git', '-C', decomp, 'ls-files', 'src'], capture_output=True, text=True).stdout.split()
    for f in files:
        m = re.search(r'func_([0-9A-Fa-f]{8})\.c', f)
        if m:
            out[m.group(1).upper()] = os.path.join(decomp, f)
    gs = os.path.join(ROOT, 'game_src')
    for n in os.listdir(gs):
        m = re.match(r'func_([0-9A-Fa-f]{8})\.c', n)
        if m:
            out[m.group(1).upper()] = os.path.join(gs, n)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--decomp', required=True)
    ap.add_argument('--game', default=os.path.join(ROOT, 'build', 'game'))
    ap.add_argument('--elf')
    a = ap.parse_args()
    img = vu0_disasm.read_image(a.elf or os.path.join(a.decomp, 'orig', 'SCUS-97328', 'CORE.GT4.elf'))
    rows = {}
    for addr, path in sorted(native_sources(a.decomp, a.game).items()):
        try:
            data = open(path, 'rb').read()
        except OSError:
            continue
        if b'vf' not in data and b'vcallms' not in data and b'vi' not in data:
            continue
        reads, writes, calls = source_regs(path, img)
        if reads or writes:
            rows[addr] = (reads, writes, calls, os.path.relpath(path, a.decomp if path.startswith(a.decomp) else ROOT))
    key = lambda r: (r[:2], int(r[2:]) if r[2:].isdigit() else 0)
    for addr, (reads, writes, calls, path) in rows.items():
        print('%s  in: %-40s out: %-40s %s%s' % (addr, ' '.join(sorted(reads, key=key)), ' '.join(sorted(writes, key=key)),
                                                ('vcallms ' + ','.join('%X' % c for c in calls) + '  ') if calls else '', path))
    # who reads what others write
    writers = collections.defaultdict(set)
    for addr, (_r, w, _c, _p) in rows.items():
        for reg in w:
            writers[reg].add(addr)
    print('\nState registers (read by a function before it writes them), with their writers:')
    shared = collections.defaultdict(set)
    for addr, (r, _w, _c, _p) in rows.items():
        for reg in r:
            shared[reg].add(addr)
    for reg in sorted(shared, key=key):
        print('  %-5s read by %-60s written by %s' % (reg, ' '.join(sorted(shared[reg])), ' '.join(sorted(writers[reg])) or '-'))


if __name__ == '__main__':
    main()
