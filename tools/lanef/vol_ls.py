#!/usr/bin/env python3
"""vol_ls.py [PREFIX]: lists GT4.VOL's files from the user's ISO, read-only, by docs/IOP_SERVICES.md 6.3 (RoFS: the
obfuscated header and page offsets, raw-deflate pages XOR 0x55, leaf nodes chained). Prints path, sector, size, real
size for paths starting with PREFIX. Local tool for N3's locator question; nothing is written."""
import struct
import sys
import zlib

ISO = "C:/Users/julie/gt4-decomp/orig/SCUS-97328/Gran Turismo 4 (USA) (v1.01).iso"
VOL_LBA = 105879
BASE = VOL_LBA + 0x2159


def read(f, lba, n, off=0):
    f.seek(lba * 2048 + off)
    return f.read(n)


def pages(f):
    hdr = read(f, BASE, 64)
    magic, version, tocSize, dataStart = struct.unpack_from("<IIII", hdr)
    pageSize, pageCount = struct.unpack_from("<HH", hdr, 0x10)
    obf = magic == 0xACB990AD
    toc = read(f, BASE, (tocSize + 0x3F) & ~0x3F)
    offs = []
    for i in range(pageCount + 1):
        w = struct.unpack_from("<I", toc, 0x40 + 4 * i)[0]
        offs.append(w ^ (((i + 1) * 0x14AC327A) & 0xFFFFFFFF) if obf else w)
    out = []
    for i in range(pageCount):
        raw = zlib.decompressobj(-15).decompress(toc[offs[i]:offs[i + 1]])
        if obf:
            raw = bytes(b ^ 0x55 for b in raw)
        out.append(raw)
    return out, pageSize, dataStart


def entries(pg, pageSize):
    for p in pg:
        tag, count = struct.unpack_from("<HH", p, 0)
        if tag != 0:
            continue
        for k in range(count):
            ko, kl, vo, vl = struct.unpack_from("<HHHH", p, pageSize - 8 * k - 8)
            key = p[ko:ko + kl]
            if kl < 4 or vl < 5 or ko + kl > pageSize or vo + vl > pageSize:
                continue   # (the page's last slot is padding)
            parent = struct.unpack_from(">I", key, 0)[0]
            yield parent, key[4:].decode("latin-1"), p[vo:vo + vl]


def listing(ents):
    """[(path, kind, sector, stored, real)]: kind 1 file, 2 compressed; directories resolved from their ids."""
    dirs = {1: ""}
    vals = []
    for parent, name, v in ents:
        if v[0] == 0 and name not in (".", ".."):
            dirs[struct.unpack_from("<I", v, 1)[0]] = (parent, name)
        vals.append((parent, name, v))

    def path(d):
        out = []
        while d in dirs and dirs[d] != "":
            d, n = dirs[d]
            out.append(n)
        return "/".join(reversed(out))
    files = []
    for parent, name, v in vals:
        if v[0] == 1:
            sector, mtime, size = struct.unpack_from("<III", v, 1)
            files.append((f"{path(parent)}/{name}".lstrip("/"), 1, sector, size, size))
        elif v[0] == 2:
            sector, mtime, stored, real = struct.unpack_from("<IIII", v, 1)
            files.append((f"{path(parent)}/{name}".lstrip("/"), 2, sector, stored, real))
    return files


with open(ISO, "rb") as f:
    pg, pageSize, dataStart = pages(f)
if "--peek" in sys.argv:
    print(f"{len(pg)} pages of {pageSize}; data start {dataStart}")
    for i in (0, 1, 2, len(pg) - 1):
        p = pg[i]
        print(f"page {i}: len {len(p)} head {p[:16].hex()}")
        print(f"   tail {p[-48:].hex()}")
        print(f"   text {p[8:120]!r}")
    raise SystemExit
if "--slots" in sys.argv:
    for i in (2, 406):
        p = pg[i]
        tag, count, nxt = struct.unpack_from("<HHi", p, 0)
        print(f"page {i}: tag {tag} count {count} next {nxt}")
        for k in list(range(4)) + [count - 1]:
            ko, kl, vo, vl = struct.unpack_from("<HHHH", p, pageSize - 8 * k - 8)
            print(f"  slot {k}: key @{ko:#x} len {kl}: {p[ko:ko + kl]!r}  value @{vo:#x} len {vl}: {p[vo:vo + vl].hex()}")
    raise SystemExit
ents = list(entries(pg, pageSize))
files = listing(ents)
if __name__ == "__main__":
    prefix = next((a for a in sys.argv[1:] if not a.startswith("--")), "")
    for path, kind, sector, stored, real in sorted(files):
        if path.startswith(prefix):
            print(f"{path}\t{'z' if kind == 2 else '-'}\t{sector}\t{stored}\t{real}")
    print(f"{len(files)} files", file=sys.stderr)
