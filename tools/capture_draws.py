"""Draw passes of one frame of a gt4 capture (gt4_game --capture): capture_draws.py CAPTURE FRAME_INDEX [unpack].

Decodes GIF packets (PATH3 records, and PATH2 = VIF1 DIRECT) and marks VU1 batches (VIF1 MSCAL,
whose PATH1 output is computed by the host), then prints the draws grouped by GS state: primitive,
FRAME, TEX0, ALPHA, TEST, vertex count and screen bounds."""
import struct
import sys

path, want = sys.argv[1], int(sys.argv[2])
SHOW_UNPACK = len(sys.argv) > 3
data = open(path, "rb").read()

PSM = {0: "CT32", 1: "CT24", 2: "CT16", 0xA: "CT16S", 0x13: "T8", 0x14: "T4", 0x1B: "T8H", 0x24: "T4HL",
       0x2C: "T4HH", 0x30: "Z32", 0x31: "Z24", 0x32: "Z16", 0x3A: "Z16S"}
PRIMS = ["point", "line", "lstrip", "tri", "tstrip", "tfan", "sprite", "?"]

state = {}
out = []
cur = None


def psm(v):
    return PSM.get(v, hex(v))


def describe():
    s = state
    prim = s.get("prim", 0)
    t = "%s%s%s" % (PRIMS[prim & 7], " tex" if prim >> 4 & 1 else "", " abe" if prim >> 6 & 1 else "")
    fr = s.get("frame", 0)
    frame = "FB %#x w%d %s msk %08x" % ((fr & 0x1FF) * 2048 * 4 // 4 // 2048, (fr >> 16) & 0x3F, psm((fr >> 24) & 0x3F), fr >> 32)
    tex = ""
    if prim >> 4 & 1:
        t0 = s.get("tex0", 0)
        tex = " TEX %#x w%d %s %dx%d tcc%d tfx%d CBP %#x %s" % (
            t0 & 0x3FFF, (t0 >> 14) & 0x3F, psm((t0 >> 20) & 0x3F), 1 << ((t0 >> 26) & 15), 1 << ((t0 >> 30) & 15),
            (t0 >> 34) & 1, (t0 >> 35) & 3, (t0 >> 37) & 0x3FFF, psm((t0 >> 51) & 15))
    al = s.get("alpha", 0)
    alpha = " A(%d%d%d%d fix %d)" % (al & 3, (al >> 2) & 3, (al >> 4) & 3, (al >> 6) & 3, (al >> 32) & 0xFF) if prim >> 6 & 1 else ""
    rgba = s.get("rgbaq", 0)
    col = " rgba %d,%d,%d,%d" % (rgba & 0xFF, (rgba >> 8) & 0xFF, (rgba >> 16) & 0xFF, (rgba >> 24) & 0xFF)
    return t + " | " + frame + tex + alpha + col


def vertex(x, y):
    global cur
    key = describe()
    if cur is None or cur[0] != key:
        cur = [key, 0, [1e9, 1e9, -1e9, -1e9]]
        out.append(cur)
    cur[1] += 1
    b = cur[2]
    b[0] = min(b[0], x); b[1] = min(b[1], y); b[2] = max(b[2], x); b[3] = max(b[3], y)


def adreg(addr, val):
    names = {0: "prim", 1: "rgbaq", 6: "tex0", 0x4C: "frame", 0x42: "alpha", 0x47: "test", 0x4E: "zbuf"}
    if addr in (5, 4, 0xC, 0xD):
        vertex(((val & 0xFFFF) - 0x6C00) / 16.0, (((val >> 16) & 0xFFFF) - 0x7900) / 16.0)
    elif addr in names:
        state[names[addr]] = val


def gif(buf, label):
    q, n = 0, len(buf) // 16
    while q < n:
        lo, hi = struct.unpack_from("<QQ", buf, q * 16)
        q += 1
        nloop, eop = lo & 0x7FFF, (lo >> 15) & 1
        if (lo >> 46) & 1:
            state["prim"] = (lo >> 47) & 0x7FF
        flg, nreg = (lo >> 58) & 3, (lo >> 60) & 15 or 16
        if flg == 0:
            for _ in range(nloop):
                for r in range(nreg):
                    if q >= n:
                        return
                    reg = (hi >> (4 * r)) & 15
                    a, b = struct.unpack_from("<QQ", buf, q * 16)
                    q += 1
                    if reg == 0xE:
                        adreg(b & 0xFF, a)
                    elif reg == 0:
                        state["prim"] = a & 0x7FF
                    elif reg == 1:
                        state["rgbaq"] = (a & 0xFF) | ((a >> 32 & 0xFF) << 8) | ((b & 0xFF) << 16) | ((b >> 32 & 0xFF) << 24)
                    elif reg == 6:
                        state["tex0"] = a
                    elif reg in (4, 5, 0xC, 0xD):
                        vertex(((a & 0xFFFF) - 0x6C00) / 16.0, (((a >> 32) & 0xFFFF) - 0x7900) / 16.0)
        elif flg == 1:
            regs = [(hi >> (4 * r)) & 15 for r in range(nreg)]
            words = nloop * nreg
            for k in range(words):
                if q * 16 + 8 * k + 8 > len(buf):
                    return
                v = struct.unpack_from("<Q", buf, q * 16 + 8 * k)[0]
                reg = regs[k % nreg]
                if reg in (4, 5, 0xC, 0xD):
                    vertex(((v & 0xFFFF) - 0x6C00) / 16.0, (((v >> 16) & 0xFFFF) - 0x7900) / 16.0)
                elif reg == 0:
                    state["prim"] = v & 0x7FF
                elif reg == 1:
                    state["rgbaq"] = v & 0xFFFFFFFF
            q += (words + 1) // 2
        else:
            q += nloop
            out.append(["  (image %d qwords, %s)" % (nloop, label), 0, None])


def vif(buf):
    global cur
    w, n = 0, len(buf) // 4
    wl, cl = 1, 1
    while w < n:
        code = struct.unpack_from("<I", buf, 4 * w)[0]
        w += 1
        cmd, num, imm = (code >> 24) & 0x7F, (code >> 16) & 0xFF, code & 0xFFFF
        if cmd == 1:
            cl, wl = imm & 0xFF, (imm >> 8) & 0xFF
        elif cmd == 0x20:
            w += 1
        elif cmd in (0x30, 0x31):
            w += 4
        elif cmd == 0x4A:
            w += (num or 256) * 2
        elif cmd in (0x50, 0x51):
            q = imm or 65536
            gif(buf[4 * w:4 * w + 16 * q], "PATH2")
            w += 4 * q
        elif cmd in (0x14, 0x15, 0x17):
            if out and out[-1][0].startswith("  VU1"):
                out[-1][1] += 1
            else:
                out.append(["  VU1 batch (PATH1 3D)", 1, None])
            cur = None
        elif cmd >= 0x60:
            vn, vl = (cmd >> 2) & 3, cmd & 3
            num = num or 256
            count = num if wl <= cl else cl * (num // wl) + min(num % wl, cl)
            nw = (count * (32 >> vl) * (vn + 1) + 31) // 32
            if SHOW_UNPACK and vn == 3 and vl == 0 and count <= 12:
                vals = struct.unpack_from("<%df" % (4 * count), buf, 4 * w)
                rows = ["(" + " ".join("%.3g" % v for v in vals[4 * r:4 * r + 4]) + ")" for r in range(count)]
                out.append(["  UNPACK V4-32 @%d x%d: %s" % (imm & 0x3FF, count, " ".join(rows)), 0, None])
            w += nw


frame, off = 0, 0
while off + 16 <= len(data):
    typ, size, seq = struct.unpack_from("<IIQ", data, off)
    payload = data[off + 16:off + 16 + size]
    off += (16 + size + 15) & ~15
    if typ == 5:
        if frame == want:
            break
        frame += 1
        out.clear()
        cur = None
        continue
    if frame != want:
        continue
    if typ == 1:
        gif(payload[4:], "PATH%d" % struct.unpack_from("<I", payload, 0)[0])
    elif typ == 2:
        vif(payload)
    elif typ == 4:
        reg, _, val = struct.unpack_from("<IIQ", payload, 0)
        out.append(["  PRIV %#x = %#x" % (reg, val), 0, None])

for key, count, b in out:
    if b is None:
        print(key, "x%d" % count if count > 1 else "")
    else:
        print("%5d v  [%6.0f,%6.0f .. %6.0f,%6.0f]  %s" % (count, b[0], b[1], b[2], b[3], key))
