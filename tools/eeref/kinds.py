#!/usr/bin/env python3
"""The parameter and return kinds of every function the native link compiles, for the GT4_ORIG bridge.

    py tools/eeref/kinds.py --decomp C:\\path\\to\\gt4-decomp [--out build/eeref/kinds.txt]

One line per PS2 function: `ADDR KINDS RET` (kinds as tools/eeref takes them, i w f q; RET v i w f), from its
definition in the source the link uses (game_src/, the matched source, else the draft; tools/abi_lint.py
parses them). GT4_ORIG_KINDS=FILE gives them to tools/eeref/orig_bridge.cpp, which needs them where native code
calls the original through a PS2 address (GT4_ORIG=all): floats go to the EE's float registers, and a float
result comes back in eax.
"""
import argparse
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import abi_lint  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--decomp', required=True)
    ap.add_argument('--game', default=os.path.join(ROOT, 'build', 'game'))
    ap.add_argument('--out', default=os.path.join(ROOT, 'build', 'eeref', 'kinds.txt'))
    a = ap.parse_args()
    rank = {'game_src': 0, 'src': 1, 'draft': 2}
    best = {}
    for path, role in abi_lint.native_sources(a.decomp, a.game).items():
        try:
            text = abi_lint.strip_comments(open(path, encoding='utf-8', errors='replace').read())
        except OSError:
            continue
        for addr, kinds, is_def, _line, _used, ret in abi_lint.scan(path, text):
            if not is_def or kinds is None:
                continue
            if addr in best and best[addr][0] <= rank[role]:
                continue
            ks = ''.join(k if k in 'ifwq' else 'i' for k in kinds if k != '...')
            best[addr] = (rank[role], ks or '-', {'s': 'v'}.get(ret, ret if ret in 'viwf' else 'i'))
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    with open(a.out, 'w', newline='\n') as f:
        for addr in sorted(best):
            f.write('%s %s %s\n' % (addr, best[addr][1], best[addr][2]))
    print('%d functions -> %s' % (len(best), a.out))


if __name__ == '__main__':
    main()
