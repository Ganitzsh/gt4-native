#!/usr/bin/env python3
"""Disassembles GT4's VU0 micro image (the one the game uploads at boot, 004A03C0) from the user's executable.

    py tools/vu0_disasm.py --elf ...\\CORE.GT4.elf [--from 0x2E8] [--to 0x460]

One line per instruction pair: byte address, upper and lower instruction, and the E (end) and I (LOI) bits.
Decodes exactly the instructions the interpreter implements (docs/NATIVE_ONLY.md: porting them to C++).
"""
import argparse
import struct

IMAGE, IMAGE_SIZE = 0x006D3280, 0xA80   # 004A03C0: func_004A12B8(0, 0, 0xA80, 0x006D3280)


def read_image(elf):
    data = open(elf, 'rb').read()
    phoff, = struct.unpack_from('<I', data, 0x1C)
    phentsize, phnum = struct.unpack_from('<HH', data, 0x2A)
    for i in range(phnum):
        typ, off, vaddr, _pa, filesz, _ms, _fl, _al = struct.unpack_from('<8I', data, phoff + i * phentsize)
        if typ == 1 and vaddr <= IMAGE and IMAGE + IMAGE_SIZE <= vaddr + filesz:
            return data[off + IMAGE - vaddr:off + IMAGE - vaddr + IMAGE_SIZE]
    raise SystemExit('the VU0 image is not in a loaded segment of %s' % elf)


def dest(w):
    d = (w >> 21) & 15
    return ''.join(c for c, b in zip('xyzw', (8, 4, 2, 1)) if d & b)


BC = 'xyzw'
KIND = ['add', 'sub', 'madd', 'msub', 'max', 'mini', 'mul']
UPPER_OP = {0x1C: ('mul', 'q'), 0x1D: ('max', 'i'), 0x1E: ('mul', 'i'), 0x1F: ('mini', 'i'), 0x20: ('add', 'q'),
            0x21: ('madd', 'q'), 0x22: ('add', 'i'), 0x23: ('madd', 'i'), 0x24: ('sub', 'q'), 0x25: ('msub', 'q'),
            0x26: ('sub', 'i'), 0x27: ('msub', 'i'), 0x28: ('add', ''), 0x29: ('madd', ''), 0x2A: ('mul', ''),
            0x2B: ('max', ''), 0x2C: ('sub', ''), 0x2D: ('msub', ''), 0x2F: ('mini', '')}
UPPER_SP = {0x1C: ('mula', 'q'), 0x1E: ('mula', 'i'), 0x20: ('adda', 'q'), 0x21: ('madda', 'q'), 0x22: ('adda', 'i'),
            0x23: ('madda', 'i'), 0x24: ('suba', 'q'), 0x25: ('msuba', 'q'), 0x26: ('suba', 'i'), 0x27: ('msuba', 'i'),
            0x28: ('adda', ''), 0x29: ('madda', ''), 0x2A: ('mula', ''), 0x2C: ('suba', ''), 0x2D: ('msuba', '')}


def upper(u):
    ft, fs, fd, bc, op = (u >> 16) & 31, (u >> 11) & 31, (u >> 6) & 31, u & 3, u & 0x3F
    d = dest(u)
    if op < 0x3C:
        if op < 0x1C:
            return '%s%s.%s vf%d, vf%d, vf%d%s' % (KIND[op >> 2], BC[bc], d, fd, fs, ft, BC[bc])
        if op == 0x2E:
            return 'opmsub.xyz vf%d, vf%d, vf%d' % (fd, fs, ft)
        if op not in UPPER_OP:
            return '?upper %08x' % u
        k, s = UPPER_OP[op]
        return '%s%s.%s vf%d, vf%d, %s' % (k, s, d, fd, fs, s if s else 'vf%d' % ft)
    sp = (((u >> 6) & 31) << 2) | bc
    if sp < 0x10:
        return '%sa%s.%s acc, vf%d, vf%d%s' % (KIND[sp >> 2], BC[bc], d, fs, ft, BC[bc])
    if sp < 0x18 or sp == 0x1D:
        names = {0x10: 'itof0', 0x11: 'itof4', 0x12: 'itof12', 0x13: 'itof15', 0x14: 'ftoi0', 0x15: 'ftoi4',
                 0x16: 'ftoi12', 0x17: 'ftoi15', 0x1D: 'abs'}
        return '%s.%s vf%d, vf%d' % (names[sp], d, ft, fs)
    if sp < 0x1C:
        return 'mula%s.%s acc, vf%d, vf%d%s' % (BC[sp & 3], d, fs, ft, BC[sp & 3])
    if sp == 0x1F:
        return 'clipw.xyz vf%d, vf%dw' % (fs, ft)
    if sp == 0x2E:
        return 'opmula.xyz acc, vf%d, vf%d' % (fs, ft)
    if sp == 0x2F:
        return 'nop'
    if sp not in UPPER_SP:
        return '?upper %08x' % u
    k, s = UPPER_SP[sp]
    return '%s%s.%s acc, vf%d, %s' % (k, s, d, fs, s if s else 'vf%d' % ft)


def lower(lo, pc):
    op = lo >> 25
    it, is_, id_ = (lo >> 16) & 31, (lo >> 11) & 31, (lo >> 6) & 31
    imm11 = lo & 0x7FF
    if imm11 & 0x400:
        imm11 -= 0x800
    d = dest(lo)
    target = '0x%X' % (pc + 8 + imm11 * 8)
    if op == 0x40:
        f = lo & 0x3F
        if f < 0x3C:
            if f == 0x32:
                imm5 = (lo >> 6) & 31
                imm5 = imm5 - 32 if imm5 & 16 else imm5
                return 'iaddi vi%d, vi%d, %d' % (it, is_, imm5)
            names = {0x30: 'iadd', 0x31: 'isub', 0x34: 'iand', 0x35: 'ior'}
            return '%s vi%d, vi%d, vi%d' % (names.get(f, '?lower'), id_, is_, it) if f in names else '?lower %08x' % lo
        sp = (((lo >> 6) & 31) << 2) | (lo & 3)
        fsf, ftf = BC[(lo >> 21) & 3], BC[(lo >> 23) & 3]
        return {
            0x30: 'move.%s vf%d, vf%d' % (d, it, is_), 0x31: 'mr32.%s vf%d, vf%d' % (d, it, is_),
            0x34: 'lqi.%s vf%d, (vi%d++)' % (d, it, is_), 0x36: 'lqd.%s vf%d, (--vi%d)' % (d, it, is_),
            0x35: 'sqi.%s vf%d, (vi%d++)' % (d, is_, it), 0x37: 'sqd.%s vf%d, (--vi%d)' % (d, is_, it),
            0x38: 'div q, vf%d%s, vf%d%s' % (is_, fsf, it, ftf), 0x39: 'sqrt q, vf%d%s' % (it, ftf),
            0x3A: 'rsqrt q, vf%d%s, vf%d%s' % (is_, fsf, it, ftf), 0x3B: 'waitq',
            0x3C: 'mtir vi%d, vf%d%s' % (it, is_, fsf), 0x3D: 'mfir.%s vf%d, vi%d' % (d, it, is_),
            0x3E: 'ilwr.%s vi%d, (vi%d)' % (d, it, is_), 0x3F: 'iswr.%s vi%d, (vi%d)' % (d, it, is_),
            0x40: 'rnext.%s vf%d, r' % (d, it), 0x41: 'rget.%s vf%d, r' % (d, it),
            0x42: 'rinit r, vf%d%s' % (is_, fsf), 0x43: 'rxor r, vf%d%s' % (is_, fsf),
        }.get(sp, '?lower %08x' % lo)
    if op == 0x00:
        return 'lq.%s vf%d, %d(vi%d)' % (d, it, imm11, is_)
    if op == 0x01:
        return 'sq.%s vf%d, %d(vi%d)' % (d, is_, imm11, it)
    if op == 0x04:
        return 'ilw.%s vi%d, %d(vi%d)' % (d, it, imm11, is_)
    if op == 0x05:
        return 'isw.%s vi%d, %d(vi%d)' % (d, it, imm11, is_)
    if op in (0x08, 0x09):
        imm15 = (lo & 0x7FF) | ((lo >> 10) & 0x7800)
        return '%s vi%d, vi%d, 0x%X' % ('iaddiu' if op == 8 else 'isubiu', it, is_, imm15)
    if op == 0x20:
        return 'b %s' % target
    if op == 0x21:
        return 'bal vi%d, %s' % (it, target)
    if op == 0x24:
        return 'jr vi%d' % is_
    if op == 0x25:
        return 'jalr vi%d, vi%d' % (it, is_)
    names = {0x28: 'ibeq', 0x29: 'ibne', 0x2C: 'ibltz', 0x2D: 'ibgtz', 0x2E: 'iblez', 0x2F: 'ibgez'}
    if op in names:
        if op in (0x28, 0x29):
            return '%s vi%d, vi%d, %s' % (names[op], it, is_, target)
        return '%s vi%d, %s' % (names[op], is_, target)
    return '?lower %08x' % lo


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--elf')
    ap.add_argument('--image', help='a micro image file instead (e.g. VU1\'s, from GT4_VU1_INVENTORY: instructions only '
                                    'VU1 has print as ?)')
    ap.add_argument('--from', dest='start', type=lambda s: int(s, 0), default=0)
    ap.add_argument('--to', type=lambda s: int(s, 0), default=IMAGE_SIZE)
    a = ap.parse_args()
    img = open(a.image, 'rb').read() if a.image else read_image(a.elf)
    for pc in range(a.start & ~7, min(a.to, len(img)), 8):
        lo, up = struct.unpack_from('<II', img, pc)
        lower_text = 'loi 0x%08X (%g)' % (lo, struct.unpack('<f', struct.pack('<I', lo))[0]) if up & 0x80000000 \
            else lower(lo, pc)
        print('%04X  %-36s %-34s%s' % (pc, upper(up & 0x7FFFFFFF), lower_text, '  [E]' if up & 0x40000000 else ''))


if __name__ == '__main__':
    main()
