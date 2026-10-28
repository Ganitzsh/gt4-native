#!/usr/bin/env python3
"""Copies eerecomp's output with a hook at every function's first instruction (docs/DIFFERENTIAL_TESTING.md).

    hook_entries.py GEN OUT --functions gt4-decomp/build/functions.csv

The translated code calls functions directly (unit_X(c, entry, depth)), so a call cannot be redirected by the
dispatcher. After each function-start label this inserts `if (gt4_orig_native(c, ADDR)) return;`: in the
native game (tools/eeref/orig_bridge.cpp) it hands the call to the native implementation when that is runtime
code (an SDK replacement, a syscall stub); in eeref it is always false. Files are written only when they
change, so the C++ build recompiles only what changed.
"""
import argparse
import csv
import os
import re

DECL = 'bool gt4_orig_native(ps2::Cpu &c, std::uint32_t addr);\n'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('gen')
    ap.add_argument('out')
    ap.add_argument('--functions', required=True)
    a = ap.parse_args()
    starts = set()
    with open(a.functions, newline='') as f:
        for row in csv.DictReader(f):
            starts.add(int(row['address'], 16))
    os.makedirs(a.out, exist_ok=True)
    label = re.compile(r'^L_([0-9A-F]{8}):\n', re.M)
    hooked = 0
    for name in sorted(os.listdir(a.gen)):
        src = os.path.join(a.gen, name)
        text = open(src, encoding='utf-8', newline='').read()
        if name.startswith('unit_'):
            def hook(m):
                nonlocal hooked
                addr = int(m.group(1), 16)
                if addr not in starts:
                    return m.group(0)
                hooked += 1
                return m.group(0) + '    if (gt4_orig_native(c, 0x%08Xu)) return;\n' % addr
            text = label.sub(hook, text)
        elif name == 'units.hpp':
            text = text.replace('namespace gen {\n', DECL + '\nnamespace gen {\n', 1)
        dst = os.path.join(a.out, name)
        if os.path.exists(dst) and open(dst, encoding='utf-8', newline='').read() == text:
            continue
        with open(dst, 'w', encoding='utf-8', newline='') as f:
            f.write(text)
    print('hooked %d function entries' % hooked)


if __name__ == '__main__':
    main()
