"""Compare data labels and global symbols of an ee-gcc .s and an i386-gt4-gcc .s of the same source.

Compiler-numbered local labels (LCn, $LCn, name.N statics) are left out; .weak (EE link-once) and
.globl count alike."""
import re, sys
EE = {'.byte': 1, '.half': 2, '.short': 2, '.word': 4, '.dword': 8}
I386 = {'.byte': 1, '.word': 2, '.value': 2, '.short': 2, '.long': 4, '.quad': 8}

def parse(path, sizes, strip):
    data, globs, cur = {}, set(), None
    for line in open(path, encoding='latin-1'):
        line = line.split('#')[0].rstrip() if not line.lstrip().startswith('.ascii') else line.rstrip()
        m = re.match(r'^([A-Za-z_.$][\w.$]*):', line)
        if m:
            cur = m.group(1)[1:] if strip and m.group(1).startswith('_') else m.group(1)
            data[cur] = []
            continue
        t = line.split(None, 1)
        if not t:
            continue
        if t[0] in ('.globl', '.global', '.weak'):
            g = t[1].strip()
            globs.add(g[1:] if strip and g.startswith('_') else g)
        elif t[0] in sizes and cur is not None:
            for v in t[1].split(','):
                v = v.strip()
                if strip and v.startswith('_') and not v[1:2].isdigit():
                    v = v[1:]
                data[cur].append((sizes[t[0]], v))
        elif t[0] in ('.space', '.zero', '.skip') and cur is not None:
            data[cur].append(('space', t[1].split(',')[0].strip()))
        elif t[0] in ('.ascii', '.asciz', '.string') and cur is not None:
            data[cur].append((t[0], t[1]))
        elif t[0].startswith('.') and t[0] not in ('.align', '.p2align', '.section', '.data', '.text', '.def', '.ent', '.end', '.frame', '.mask', '.fmask', '.set', '.linkonce', '.rdata', '.type', '.size', '.file', '.previous', '.comm', '.lcomm', '.local', '.weak', '.sdata', '.globl', '.ident', '.loc', '.stabs', '.stabn', '.bss'):
            pass
        elif not t[0].startswith('.'):
            cur = None  # code
    return {k: v for k, v in data.items() if v and not re.match(r'^\$?LC\d+$|^\w+\.\d+$', k)}, globs

ee, ge = parse(sys.argv[1], EE, False)
ix, gi = parse(sys.argv[2], I386, True)
diff = 0
for k in sorted(set(ee) | set(ix)):
    a, b = ee.get(k), ix.get(k)
    if a != b:
        diff += 1
        print(f'DIFF {k}: EE={a} I386={b}')
    elif '-v' in sys.argv:
        print(f'same {k}: {a}')
print('globals only EE:', sorted(ge - gi))
print('globals only i386:', sorted(gi - ge))
print('common globals:', len(ge & gi), 'data labels:', len(set(ee) | set(ix)), 'differences:', diff)
