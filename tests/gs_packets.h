#pragma once
// Builders for synthetic GIF / VIF streams used by the PS2 graphics tests. Register layouts from the
// public GS documentation.
#include <cstdint>
#include <cstring>
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

}  // namespace gt4::test
