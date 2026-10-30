#pragma once
// Builders for synthetic GIF / VIF streams used by the PS2 graphics tests. Register layouts from the
// public GS documentation.
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace gt4::test {

enum : uint8_t {
    PRIM = 0x00, RGBAQ = 0x01, ST = 0x02, UV = 0x03, XYZF2 = 0x04, XYZ2 = 0x05, TEX0_1 = 0x06, CLAMP_1 = 0x08,
    TEX1_1 = 0x14, XYOFFSET_1 = 0x18, PRMODECONT = 0x1A, TEXCLUT = 0x1C, TEXA = 0x3B, TEXFLUSH = 0x3F,
    SCISSOR_1 = 0x40, ALPHA_1 = 0x42, DTHE = 0x45, COLCLAMP = 0x46, TEST_1 = 0x47, PABE = 0x49, FBA_1 = 0x4A,
    FRAME_1 = 0x4C, ZBUF_1 = 0x4E, BITBLTBUF = 0x50, TRXPOS = 0x51, TRXREG = 0x52, TRXDIR = 0x53,
    SIGNAL = 0x60, FINISH = 0x61, LABEL = 0x62,
};
enum : uint32_t { PSMCT32 = 0x00, PSMCT24 = 0x01, PSMCT16 = 0x02, PSMT8 = 0x13, PSMT4 = 0x14, PSMZ32 = 0x30, PSMZ24 = 0x31 };

constexpr uint64_t frame(uint32_t fbp, uint32_t fbw, uint32_t psm, uint32_t mask = 0) {
    return uint64_t(fbp) | (uint64_t(fbw) << 16) | (uint64_t(psm) << 24) | (uint64_t(mask) << 32);
}
constexpr uint64_t zbuf(uint32_t zbp, uint32_t psm, bool mask) {
    return uint64_t(zbp) | (uint64_t(psm & 0xF) << 24) | (uint64_t(mask) << 32);
}
constexpr uint64_t scissor(uint32_t x0, uint32_t x1, uint32_t y0, uint32_t y1) {
    return uint64_t(x0) | (uint64_t(x1) << 16) | (uint64_t(y0) << 32) | (uint64_t(y1) << 48);
}
constexpr uint64_t xyoffset(uint32_t ofx, uint32_t ofy) { return uint64_t(ofx) | (uint64_t(ofy) << 32); }
constexpr uint64_t test(bool zte, uint32_t ztst) { return (uint64_t(zte) << 16) | (uint64_t(ztst) << 17); }
constexpr uint64_t prim(uint32_t type, bool iip, bool tme, bool abe, bool fst) {
    return uint64_t(type) | (uint64_t(iip) << 3) | (uint64_t(tme) << 4) | (uint64_t(abe) << 6) | (uint64_t(fst) << 8);
}
constexpr uint64_t rgbaq(uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
    return uint64_t(r) | (uint64_t(g) << 8) | (uint64_t(b) << 16) | (uint64_t(a) << 24) | (uint64_t(0x3F800000) << 32);
}
// Window coordinates in pixels (12.4 fixed point is applied here), with the usual 2048 offset.
constexpr uint64_t xyz(uint32_t x, uint32_t y, uint32_t z = 0) {
    return uint64_t((x + 2048) << 4) | (uint64_t((y + 2048) << 4) << 16) | (uint64_t(z) << 32);
}
constexpr uint64_t uv(uint32_t u, uint32_t v) { return uint64_t(u << 4) | (uint64_t(v << 4) << 16); }
constexpr uint64_t tex0(uint32_t tbp, uint32_t tbw, uint32_t psm, uint32_t tw, uint32_t th, bool tcc, uint32_t tfx,
                        uint32_t cbp = 0, uint32_t cpsm = 0, uint32_t csm = 0, uint32_t csa = 0, uint32_t cld = 0) {
    return uint64_t(tbp) | (uint64_t(tbw) << 14) | (uint64_t(psm) << 20) | (uint64_t(tw) << 26) | (uint64_t(th) << 30) |
           (uint64_t(tcc) << 34) | (uint64_t(tfx) << 35) | (uint64_t(cbp) << 37) | (uint64_t(cpsm) << 51) |
           (uint64_t(csm) << 55) | (uint64_t(csa) << 56) | (uint64_t(cld) << 61);
}
constexpr uint64_t bitbltbuf(uint32_t dbp, uint32_t dbw, uint32_t dpsm, uint32_t sbp = 0, uint32_t sbw = 0, uint32_t spsm = 0) {
    return uint64_t(sbp) | (uint64_t(sbw) << 16) | (uint64_t(spsm) << 24) | (uint64_t(dbp) << 32) | (uint64_t(dbw) << 48) |
           (uint64_t(dpsm) << 56);
}
constexpr uint64_t trxpos(uint32_t dx, uint32_t dy, uint32_t sx = 0, uint32_t sy = 0) {
    return uint64_t(sx) | (uint64_t(sy) << 16) | (uint64_t(dx) << 32) | (uint64_t(dy) << 48);
}
constexpr uint64_t trxreg(uint32_t w, uint32_t h) { return uint64_t(w) | (uint64_t(h) << 32); }

// Privileged registers (offsets from 0x12000000).
constexpr uint64_t pmode(bool en1, bool en2, bool slbg = false, uint32_t alp = 0xFF) {
    return uint64_t(en1) | (uint64_t(en2) << 1) | (1ull << 2) | (1ull << 5) | (uint64_t(slbg) << 7) | (uint64_t(alp) << 8);
}
constexpr uint64_t smode2(bool interlace, bool ffmd) { return uint64_t(interlace) | (uint64_t(ffmd) << 1); }
constexpr uint64_t dispfb(uint32_t fbp, uint32_t fbw, uint32_t psm) {
    return uint64_t(fbp) | (uint64_t(fbw) << 9) | (uint64_t(psm) << 15);
}
// NTSC 640 x 448 interlaced frame mode, like sceGsSetDefDispEnv.
constexpr uint64_t display640x448() {
    return uint64_t(636) | (uint64_t(50) << 12) | (uint64_t(3) << 23) | (uint64_t(2559) << 32) | (uint64_t(447) << 44);
}

// A GIF packet: GIFtags and data qwords.
struct Gif {
    std::vector<uint64_t> q;   // pairs (lo, hi)
    void qword(uint64_t lo, uint64_t hi) { q.push_back(lo); q.push_back(hi); }
    // PACKED A+D: one register write per qword.
    void ad(std::initializer_list<std::pair<uint8_t, uint64_t>> writes, bool eop = true) {
        qword(uint64_t(writes.size()) | (uint64_t(eop) << 15) | (1ull << 60), 0xE);
        for (const auto& w : writes) qword(w.second, w.first);
    }
    // PRIM through A+D, then a REGLIST of vertices with the given registers (register descriptors: GS
    // addresses). The GIFtag's PRE/PRIM fields only apply in PACKED mode, so PRIM is written first.
    void reglist(uint64_t primValue, std::initializer_list<uint8_t> regs, const std::vector<uint64_t>& values,
                 bool eop = true) {
        ad({{PRIM, primValue}}, false);
        const uint32_t nreg = static_cast<uint32_t>(regs.size());
        const uint32_t nloop = static_cast<uint32_t>(values.size() / nreg);
        uint64_t descriptors = 0, shift = 0;
        for (uint8_t r : regs) { descriptors |= uint64_t(r) << shift; shift += 4; }
        qword(uint64_t(nloop) | (uint64_t(eop) << 15) | (1ull << 58) | (uint64_t(nreg & 0xF) << 60), descriptors);
        for (size_t i = 0; i < values.size(); i += 2) qword(values[i], i + 1 < values.size() ? values[i + 1] : 0);
    }
    // IMAGE data: the bytes, padded to qwords.
    void image(const void* data, size_t bytes, bool eop = true) {
        const size_t qwc = (bytes + 15) / 16;
        qword(uint64_t(qwc) | (uint64_t(eop) << 15) | (2ull << 58), 0);
        const size_t first = q.size();
        q.resize(first + qwc * 2, 0);
        std::memcpy(q.data() + first, data, bytes);
    }
    // A host -> local upload of a w x h rectangle at (dbp, dbw, psm).
    void upload(uint32_t dbp, uint32_t dbw, uint32_t psm, uint32_t w, uint32_t h, const void* data, size_t bytes) {
        ad({{BITBLTBUF, bitbltbuf(dbp, dbw, psm)}, {TRXPOS, trxpos(0, 0)}, {TRXREG, trxreg(w, h)}, {TRXDIR, 0}}, false);
        image(data, bytes);
    }
    size_t bytes() const { return q.size() * 8; }
    const void* data() const { return q.data(); }
};

constexpr uint32_t rgb(uint32_t r, uint32_t g, uint32_t b) { return r | (g << 8) | (b << 16); }

// The tests' scene: context 1 draws into a 640 x 448 PSMCT32 frame at 0, no Z, whole-screen scissor.
inline Gif setup() {
    Gif g;
    g.ad({{FRAME_1, frame(0, 10, PSMCT32)}, {ZBUF_1, zbuf(0x70, PSMZ24, true)}, {SCISSOR_1, scissor(0, 639, 0, 447)},
          {XYOFFSET_1, xyoffset(2048 << 4, 2048 << 4)}, {TEST_1, test(true, 1)}, {PRMODECONT, 1}, {DTHE, 0},
          {COLCLAMP, 1}, {TEX1_1, 0}});
    return g;
}

inline void sprite(Gif& g, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, uint64_t color, bool abe = false) {
    g.reglist(prim(6, false, false, abe, false), {RGBAQ, XYZ2, XYZ2}, {color, xyz(x0, y0), xyz(x1, y1)});
}

// Privileged registers of a 640 x 448 interlaced display of the frame at 0. Interlaced with a full-height
// buffer read every other line per field (FFMD = 0).
inline constexpr std::pair<uint32_t, uint64_t> kDisplay[] = {
    {0x00, pmode(true, false)}, {0x20, smode2(true, false)}, {0x70, dispfb(0, 10, PSMCT32)}, {0x80, display640x448()}, {0xE0, 0}};

// --- VU1 microcode (encodings from the public VU instruction set) ---------------------------------------
namespace vu {
constexpr uint32_t X = 8, Y = 4, Z = 2, W = 1, XYZW = 15;
constexpr uint32_t kNopUpper = 0x000002FF;   // NOP
constexpr uint32_t kNopLower = 0x8000033C;   // MOVE vf0, vf0 with an empty field mask
constexpr uint32_t kEbit = 0x40000000;
constexpr uint32_t upper(uint32_t op, uint32_t dest, uint32_t ft, uint32_t fs, uint32_t fd) {
    return (dest << 21) | (ft << 16) | (fs << 11) | (fd << 6) | op;
}
constexpr uint32_t upperExt(uint32_t ext, uint32_t dest, uint32_t ft, uint32_t fs) {
    return (dest << 21) | (ft << 16) | (fs << 11) | ((ext >> 2) << 6) | 0x3C | (ext & 3);
}
constexpr uint32_t ADD(uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft) { return upper(0x28, dest, ft, fs, fd); }
constexpr uint32_t MULbc(uint32_t bc, uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft) { return upper(0x18 | bc, dest, ft, fs, fd); }
constexpr uint32_t MADDbc(uint32_t bc, uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft) { return upper(0x08 | bc, dest, ft, fs, fd); }
constexpr uint32_t MULq(uint32_t dest, uint32_t fd, uint32_t fs) { return upper(0x1C, dest, 0, fs, fd); }
constexpr uint32_t MULAbc(uint32_t bc, uint32_t dest, uint32_t fs, uint32_t ft) { return upperExt(0x18 | bc, dest, ft, fs); }
constexpr uint32_t MADDAbc(uint32_t bc, uint32_t dest, uint32_t fs, uint32_t ft) { return upperExt(0x08 | bc, dest, ft, fs); }
constexpr uint32_t FTOI4(uint32_t dest, uint32_t ft, uint32_t fs) { return upperExt(0x15, dest, ft, fs); }
constexpr uint32_t ITOF0(uint32_t dest, uint32_t ft, uint32_t fs) { return upperExt(0x10, dest, ft, fs); }
constexpr uint32_t CLIPw(uint32_t fs, uint32_t ft) { return upperExt(0x1F, 0xE, ft, fs); }
constexpr uint32_t imm11(int32_t v) { return static_cast<uint32_t>(v) & 0x7FF; }
constexpr uint32_t LQ(uint32_t dest, uint32_t ft, int32_t offset, uint32_t is) { return (dest << 21) | (ft << 16) | (is << 11) | imm11(offset); }
constexpr uint32_t SQ(uint32_t dest, uint32_t fs, int32_t offset, uint32_t it) { return (1u << 25) | (dest << 21) | (it << 16) | (fs << 11) | imm11(offset); }
constexpr uint32_t ISW(uint32_t dest, uint32_t it, int32_t offset, uint32_t is) { return (5u << 25) | (dest << 21) | (it << 16) | (is << 11) | imm11(offset); }
constexpr uint32_t IADDIU(uint32_t it, uint32_t is, uint32_t imm) {
    return (8u << 25) | (((imm >> 11) & 0xF) << 21) | (it << 16) | (is << 11) | (imm & 0x7FF);
}
constexpr uint32_t ISUBIU(uint32_t it, uint32_t is, uint32_t imm) {
    return (9u << 25) | (((imm >> 11) & 0xF) << 21) | (it << 16) | (is << 11) | (imm & 0x7FF);
}
constexpr uint32_t FCAND(uint32_t imm24) { return (0x12u << 25) | (imm24 & 0xFFFFFF); }
constexpr uint32_t IBNE(uint32_t it, uint32_t is, int32_t offset) { return (0x29u << 25) | (it << 16) | (is << 11) | imm11(offset); }
constexpr uint32_t lowerExt(uint32_t ext, uint32_t dest, uint32_t ft, uint32_t fs) {
    return (0x40u << 25) | (dest << 21) | (ft << 16) | (fs << 11) | ((ext >> 2) << 6) | 0x3C | (ext & 3);
}
constexpr uint32_t LQI(uint32_t dest, uint32_t ft, uint32_t is) { return lowerExt(0x34, dest, ft, is); }
constexpr uint32_t SQI(uint32_t dest, uint32_t fs, uint32_t it) { return lowerExt(0x35, dest, it, fs); }
constexpr uint32_t DIV(uint32_t fs, uint32_t fsf, uint32_t ft, uint32_t ftf) {
    return (0x40u << 25) | (ftf << 23) | (fsf << 21) | (ft << 16) | (fs << 11) | (14u << 6) | 0x3C;
}
constexpr uint32_t WAITQ() { return lowerExt(0x3B, 0, 0, 0); }
constexpr uint32_t WAITP() { return lowerExt(0x7B, 0, 0, 0); }
constexpr uint32_t ESADD(uint32_t fs) { return lowerExt(0x70, 0, 0, fs); }
constexpr uint32_t MFP(uint32_t dest, uint32_t ft) { return lowerExt(0x64, dest, ft, 0); }
constexpr uint32_t MR32(uint32_t dest, uint32_t ft, uint32_t fs) { return lowerExt(0x31, dest, ft, fs); }
constexpr uint32_t LQD(uint32_t dest, uint32_t ft, uint32_t is) { return lowerExt(0x36, dest, ft, is); }
constexpr uint32_t SQD(uint32_t dest, uint32_t fs, uint32_t it) { return lowerExt(0x37, dest, it, fs); }
constexpr uint32_t BAL(uint32_t it, int32_t offset) { return (0x21u << 25) | (it << 16) | imm11(offset); }
constexpr uint32_t JR(uint32_t is) { return (0x24u << 25) | (is << 11); }
constexpr uint32_t OPMULA(uint32_t fs, uint32_t ft) { return upperExt(0x2E, 0xE, ft, fs); }
constexpr uint32_t OPMSUB(uint32_t fd, uint32_t fs, uint32_t ft) { return upper(0x2E, 0xE, ft, fs, fd); }
constexpr uint32_t MAX(uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft) { return upper(0x2B, dest, ft, fs, fd); }
constexpr uint32_t MINIi(uint32_t dest, uint32_t fd, uint32_t fs) { return upper(0x1F, dest, 0, fs, fd); }
constexpr uint32_t MULi(uint32_t dest, uint32_t fd, uint32_t fs) { return upper(0x1E, dest, 0, fs, fd); }
constexpr uint32_t kIbit = 0x80000000;
constexpr uint32_t XGKICK(uint32_t is) { return lowerExt(0x6C, 0, 0, is); }
constexpr uint32_t XTOP(uint32_t it) { return lowerExt(0x68, 0, it, 0); }

// A microprogram: (upper, lower) pairs.
struct Program {
    std::vector<uint32_t> words;   // lower, upper, lower, upper ...
    Program& op(uint32_t upperWord, uint32_t lowerWord) { words.push_back(lowerWord); words.push_back(upperWord); return *this; }
    Program& lower(uint32_t lowerWord) { return op(kNopUpper, lowerWord); }
    Program& up(uint32_t upperWord) { return op(upperWord, kNopLower); }
    // The last instruction: the E bit, then the delay slot.
    Program& end(uint32_t upperWord = kNopUpper, uint32_t lowerWord = kNopLower) {
        op(upperWord | kEbit, lowerWord);
        return op(kNopUpper, kNopLower);
    }
    uint32_t size() const { return static_cast<uint32_t>(words.size() / 2); }
};

// Transforms N vertices (VU data qwords 0..N-1: x, y, z, 1 floats in pixels) by the matrix in qwords
// 4..7 (columns; column 3 is the translation), converts them to 12.4 fixed point and XGKICKs one
// PACKED GIF packet: the GIFtag template in qword 9, then RGBAQ (qword 8) and XYZ2 per vertex, built at 16.
inline Program transformProgram(uint32_t vertices) {
    Program p;
    p.lower(LQ(XYZW, 4, 4, 0)).lower(LQ(XYZW, 5, 5, 0)).lower(LQ(XYZW, 6, 6, 0)).lower(LQ(XYZW, 7, 7, 0));
    p.lower(LQ(XYZW, 8, 8, 0)).lower(LQ(XYZW, 9, 9, 0)).lower(SQ(XYZW, 9, 16, 0));
    p.lower(IADDIU(1, 0, 0)).lower(IADDIU(2, 0, 17)).lower(IADDIU(3, 0, vertices));
    const int32_t loop = static_cast<int32_t>(p.size());
    p.lower(LQI(XYZW, 1, 1));
    p.up(MULAbc(0, XYZW, 4, 1)).up(MADDAbc(1, XYZW, 5, 1)).up(MADDAbc(2, XYZW, 6, 1)).up(MADDbc(3, XYZW, 2, 7, 1));
    p.up(FTOI4(XYZW, 3, 2));
    p.lower(SQI(XYZW, 8, 2)).lower(SQI(XYZW, 3, 2));
    p.lower(ISUBIU(3, 3, 1)).lower(kNopLower);   // the NOP: a branch right after ISUBIU would read the old VI3
    const int32_t branchAt = static_cast<int32_t>(p.size());
    p.lower(IBNE(3, 0, loop - (branchAt + 1))).lower(kNopLower);
    p.lower(IADDIU(4, 0, 16));
    p.end(kNopUpper, XGKICK(4));
    return p;
}
}  // namespace vu

// VIF codes.
namespace vif {
constexpr uint32_t code(uint32_t cmd, uint32_t num, uint32_t imm) { return (cmd << 24) | ((num & 0xFF) << 16) | (imm & 0xFFFF); }
constexpr uint32_t NOP() { return 0; }
constexpr uint32_t STCYCL(uint32_t cl, uint32_t wl) { return code(0x01, 0, cl | (wl << 8)); }
constexpr uint32_t OFFSET(uint32_t ofst) { return code(0x02, 0, ofst); }
constexpr uint32_t BASE(uint32_t base) { return code(0x03, 0, base); }
constexpr uint32_t ITOP(uint32_t v) { return code(0x04, 0, v); }
constexpr uint32_t STMOD(uint32_t mode) { return code(0x05, 0, mode); }
constexpr uint32_t MSCAL(uint32_t address) { return code(0x14, 0, address); }
constexpr uint32_t FLUSH() { return code(0x11, 0, 0); }
constexpr uint32_t STMASK() { return code(0x20, 0, 0); }
constexpr uint32_t STROW() { return code(0x30, 0, 0); }
constexpr uint32_t STCOL() { return code(0x31, 0, 0); }
constexpr uint32_t MPG(uint32_t count, uint32_t address) { return code(0x4A, count, address); }
constexpr uint32_t DIRECT(uint32_t qwc) { return code(0x50, 0, qwc); }
// UNPACK: vn 0..3 (S, V2, V3, V4), vl 0..3 (32, 16, 8, 5-bit), masked, usn, flg (add TOPS).
constexpr uint32_t UNPACK(uint32_t vn, uint32_t vl, uint32_t num, uint32_t address, bool masked = false, bool usn = false,
                          bool flg = false) {
    return code(0x60 | (masked ? 0x10 : 0) | (vn << 2) | vl, num, address | (usn ? 0x4000 : 0) | (flg ? 0x8000 : 0));
}
struct Stream {
    std::vector<uint32_t> words;
    Stream& add(uint32_t w) { words.push_back(w); return *this; }
    Stream& add(const void* data, size_t bytes) {
        const size_t first = words.size();
        words.resize(first + (bytes + 3) / 4, 0);
        std::memcpy(words.data() + first, data, bytes);
        return *this;
    }
    // Pads with NOPs so that the next code is the last word of a qword (its data then starts aligned).
    Stream& alignForData() { while (words.size() % 4 != 3) words.push_back(0); return *this; }
    Stream& mpg(const vu::Program& program, uint32_t address) {
        alignForData();
        add(MPG(program.size(), address));
        return add(program.words.data(), program.words.size() * 4);
    }
    const uint8_t* bytes() const { return reinterpret_cast<const uint8_t*>(words.data()); }
    size_t size() const { return words.size() * 4; }
};
}  // namespace vif

inline uint32_t floatBits(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }

// The VIF1 stream of the transform test: upload the microprogram, the vertices, matrix, colour and GIF
// tag, then MSCAL. Draws a triangle (x0,y0) (x1,y1) (x2,y2) in the given colour through PATH1.
inline vif::Stream transformStream(const float (&xy)[3][2], uint32_t r, uint32_t g, uint32_t b) {
    vif::Stream s;
    s.add(vif::STCYCL(1, 1));
    s.mpg(vu::transformProgram(3), 0);
    float vertices[3][4];
    for (int k = 0; k < 3; ++k) { vertices[k][0] = xy[k][0]; vertices[k][1] = xy[k][1]; vertices[k][2] = 0; vertices[k][3] = 1; }
    const float matrix[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {2048, 2048, 0, 0}};
    const uint32_t color[4] = {r, g, b, 0x80};
    // GIFtag: NLOOP 3, EOP, PRE with PRIM = triangle, PACKED, NREG 2: RGBAQ (1), XYZ2 (5).
    const uint64_t tag[2] = {3ull | (1ull << 15) | (1ull << 46) | (3ull << 47) | (2ull << 60), 0x51};
    s.alignForData().add(vif::UNPACK(3, 0, 3, 0)).add(vertices, sizeof(vertices));
    s.alignForData().add(vif::UNPACK(3, 0, 4, 4)).add(matrix, sizeof(matrix));
    s.alignForData().add(vif::UNPACK(3, 0, 1, 8)).add(color, sizeof(color));
    s.alignForData().add(vif::UNPACK(3, 0, 1, 9)).add(tag, sizeof(tag));
    s.add(vif::MSCAL(0));
    return s;
}

}  // namespace gt4::test
