#!/usr/bin/env python3
"""vol_cat.py PATH OUT: one GT4.VOL file from the user's ISO, inflated (IOP_SERVICES.md 6.3: LBA = base + data start +
sector; a compressed file is u32 0xFFF7EEC5, s32 -realSize, raw deflate), written to OUT. Local: game data stays in
build/ and is never committed."""
import struct
import sys
import zlib

sys.argv, args = sys.argv[:1], sys.argv[1:]
import vol_ls  # noqa: E402  (lists nothing without arguments other than --flags; prints a count to stderr)

path, out = args
files = {p: (k, s, st, r) for p, k, s, st, r in vol_ls.files}
kind, sector, stored, real = files[path]
with open(vol_ls.ISO, "rb") as f:
    raw = vol_ls.read(f, vol_ls.BASE + vol_ls.dataStart + sector, stored)
if kind == 2:
    magic, neg = struct.unpack_from("<Ii", raw, 0)
    assert magic == 0xFFF7EEC5 and -neg == real, (hex(magic), neg, real)
    raw = zlib.decompressobj(-15).decompress(raw[8:])
    assert len(raw) == real, (len(raw), real)
open(out, "wb").write(raw)
print(f"{path}: {len(raw)} bytes -> {out}")
