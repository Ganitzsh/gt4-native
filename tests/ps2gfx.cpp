// PS2 graphics front end on the CPU (no GPU): the VU1 interpreter, VIF1 decoding, the graphics stream
// records, capture replay and the pad encoding. The GS itself is checked by tests/gs_backend.cpp.
#include "gs_packets.h"
#include "link_host.h"
#include "ps2_pad.h"
#include "stream.h"
#include "vif1.h"
#include "vu1.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace gt4::test;
using gt4::ps2::Vu1;

namespace {

int failures = 0;
void expect(bool ok, const char* what) {
    if (!ok) { std::fprintf(stderr, "FAILED: %s\n", what); ++failures; }
}

struct Recorder final : gt4::ps2::GsTarget {
    struct Packet { uint32_t path; std::vector<uint8_t> bytes; };
    std::vector<Packet> packets;
    std::vector<std::pair<uint32_t, uint64_t>> priv;
    std::vector<uint32_t> vsyncs;
    void gif(uint32_t path, const uint8_t* data, size_t bytes) override { packets.push_back({path, {data, data + bytes}}); }
    void privileged(uint32_t offset, uint64_t value) override { priv.emplace_back(offset, value); }
    void vsync(uint32_t field, uint32_t) override { vsyncs.push_back(field); }
    void readTransfer(void* out, uint32_t qwc) override {
        auto* w = static_cast<uint32_t*>(out);
        for (uint32_t k = 0; k < qwc * 4; ++k) w[k] = 0xA0000000u + k;
    }
};

void load(Vu1& vu, const vu::Program& program, uint32_t at = 0) {
    std::memcpy(vu.micro + at * 8, program.words.data(), program.words.size() * 4);
}
void setQword(Vu1& vu, uint32_t qw, float x, float y, float z, float w) {
    const float v[4] = {x, y, z, w};
    std::memcpy(vu.data + qw * 16, v, 16);
}
float dataFloat(const Vu1& vu, uint32_t qw, uint32_t c) {
    float f;
    std::memcpy(&f, vu.data + qw * 16 + c * 4, 4);
    return f;
}
uint32_t dataWord(const Vu1& vu, uint32_t qw, uint32_t c) {
    uint32_t u;
    std::memcpy(&u, vu.data + qw * 16 + c * 4, 4);
    return u;
}
bool vecEquals(const Vu1& vu, uint32_t qw, float x, float y, float z, float w) {
    return dataFloat(vu, qw, 0) == x && dataFloat(vu, qw, 1) == y && dataFloat(vu, qw, 2) == z && dataFloat(vu, qw, 3) == w;
}

void testArithmetic() {
    using namespace vu;
    Vu1 unit;
    setQword(unit, 0, 1, 2, 3, 4);
    setQword(unit, 1, 0.5f, 0.5f, 0.5f, 2);
    Program p;
    p.lower(LQ(XYZW, 1, 0, 0)).lower(LQ(XYZW, 2, 1, 0));
    p.up(ADD(XYZW, 3, 1, 2));                       // (1.5, 2.5, 3.5, 6)
    p.up(MULbc(3, XYZW, 4, 1, 2));                  // * vf2.w = (2, 4, 6, 8)
    p.up(MULAbc(0, XYZW, 1, 2));                    // ACC = vf1 * 0.5
    p.up(MADDbc(1, XYZW, 5, 1, 2));                 // ACC + vf1 * 0.5 = (1, 2, 3, 4)
    p.lower(DIV(1, 0, 2, 3));                       // Q = 1 / 2
    p.lower(WAITQ());
    p.up(MULq(XYZW, 6, 1));                         // (0.5, 1, 1.5, 2)
    p.up(FTOI4(XYZW, 7, 1));                        // (16, 32, 48, 64)
    p.lower(SQ(XYZW, 3, 2, 0)).lower(SQ(XYZW, 4, 3, 0)).lower(SQ(XYZW, 5, 4, 0)).lower(SQ(XYZW, 6, 5, 0));
    p.end(kNopUpper, SQ(XYZW, 7, 6, 0));
    load(unit, p);
    const uint64_t cycles = unit.run(0);
    expect(!unit.runaway(), "arithmetic program ends at the E bit");
    expect(vecEquals(unit, 2, 1.5f, 2.5f, 3.5f, 6), "ADD");
    expect(vecEquals(unit, 3, 2, 4, 6, 8), "MULw (broadcast)");
    expect(vecEquals(unit, 4, 1, 2, 3, 4), "MULAx + MADDy through ACC");
    expect(vecEquals(unit, 5, 0.5f, 1, 1.5f, 2), "DIV + WAITQ + MULq");
    expect(dataWord(unit, 6, 0) == 16 && dataWord(unit, 6, 3) == 64, "FTOI4");
    // 16 pairs, plus the FMAC (4-cycle) and DIV (7-cycle) stalls.
    expect(cycles > 16 && cycles < 60, "cycle count includes the pipeline stalls");
}

void testMoreInstructions() {
    using namespace vu;
    Vu1 unit;
    setQword(unit, 0, 1, 0, 0, 0);
    setQword(unit, 1, 0, 1, 0, 0);
    setQword(unit, 2, -3, 5, 2, 7);
    const uint32_t ints[4] = {3, static_cast<uint32_t>(-2), 0, 1};
    std::memcpy(unit.data + 3 * 16, ints, 16);
    Program p;
    p.lower(LQ(XYZW, 1, 0, 0)).lower(LQ(XYZW, 2, 1, 0)).lower(LQ(XYZW, 3, 2, 0)).lower(LQ(XYZW, 9, 3, 0));
    p.up(OPMULA(1, 2));                                    // ACC.xyz = vf1.yzx * vf2.zxy
    p.up(OPMSUB(4, 2, 1));                                 // vf4.xyz = ACC - vf2.yzx * vf1.zxy = vf1 x vf2
    p.op(MAX(XYZW, 5, 3, 0) | kIbit, floatBits(4.0f));     // MAX vf5 = max(vf3, vf0 = (0,0,0,1)); the I bit sets I = 4
    p.up(MINIi(XYZW, 6, 3));                               // min(vf3, I = 4)
    p.up(MULi(XYZW, 7, 3));                                // vf3 * 4
    p.up(ITOF0(XYZW, 8, 9));                               // (3, -2, 0, 1)
    p.lower(MR32(XYZW, 10, 3));                            // (5, 2, 7, -3)
    p.lower(ESADD(3)).lower(WAITP()).lower(MFP(X, 11));    // 9 + 25 + 4 = 38
    p.lower(IADDIU(5, 0, 20)).lower(SQD(XYZW, 4, 5));      // pre-decrement: store at 19
    p.lower(IADDIU(6, 0, 20)).lower(LQD(XYZW, 12, 6));     // reload from 19
    p.lower(BAL(15, 6)).lower(kNopLower);                  // call the subroutine after the end (link: after the delay slot)
    p.lower(SQ(XYZW, 5, 20, 0)).lower(SQ(XYZW, 6, 21, 0)).lower(SQ(XYZW, 7, 22, 0));
    p.end(kNopUpper, SQ(XYZW, 8, 23, 0));
    // subroutine: store, return
    p.lower(SQ(XYZW, 10, 24, 0)).lower(SQ(X, 11, 25, 0)).lower(SQ(XYZW, 12, 26, 0));
    p.lower(JR(15)).lower(kNopLower);
    load(unit, p);
    unit.run(0);
    expect(!unit.runaway(), "program with a call ends");
    expect(vecEquals(unit, 19, 0, 0, 1, 0), "OPMULA + OPMSUB give the cross product");
    expect(vecEquals(unit, 20, 0, 5, 2, 7), "MAX against vf0");
    expect(vecEquals(unit, 21, -3, 4, 2, 4), "MINIi with the I-bit immediate");
    expect(vecEquals(unit, 22, -12, 20, 8, 28), "MULi");
    expect(vecEquals(unit, 23, 3, -2, 0, 1), "ITOF0");
    expect(vecEquals(unit, 24, 5, 2, 7, -3), "MR32 rotates");
    expect(dataFloat(unit, 25, 0) == 38.0f, "ESADD + WAITP + MFP");
    expect(vecEquals(unit, 26, 0, 0, 1, 0), "SQD / LQD pre-decrement");
}

// A counted loop; with `quirk` the branch directly follows the ISUBIU and reads the previous VI value
// (the VU's integer-branch timing, as PCSX2 models it), so the loop runs once more.
uint32_t loopSum(bool quirk) {
    using namespace vu;
    Vu1 unit;
    Program p;
    p.lower(IADDIU(1, 0, 5)).lower(IADDIU(2, 0, 0));
    const int32_t loop = static_cast<int32_t>(p.size());
    p.lower(IADDIU(2, 2, 3)).lower(ISUBIU(1, 1, 1));
    if (!quirk) p.lower(kNopLower);
    const int32_t branch = static_cast<int32_t>(p.size());
    p.lower(IBNE(1, 0, loop - (branch + 1))).lower(kNopLower);
    p.end(kNopUpper, ISW(X, 2, 7, 0));
    load(unit, p);
    unit.run(0);
    return dataWord(unit, 7, 0);
}

void testBranches() {
    expect(loopSum(false) == 15, "IBNE loop with the branch delay slot");
    expect(loopSum(true) == 18, "a branch right after the VI write reads the previous value");
}

void testFlags() {
    using namespace vu;
    Vu1 unit;
    setQword(unit, 0, 2, 0, 0, 1);   // x > w: CLIP sets +x (bit 0)
    Program p;
    p.lower(LQ(XYZW, 1, 0, 0));
    p.up(CLIPw(1, 1));
    p.lower(FCAND(1));                       // 1 cycle later: the clip flag is not visible yet
    p.lower(ISW(X, 1, 8, 0));
    p.lower(kNopLower).lower(kNopLower);
    p.lower(FCAND(1));                       // 4+ cycles later: visible
    p.end(kNopUpper, ISW(X, 1, 9, 0));
    load(unit, p);
    unit.run(0);
    expect(dataWord(unit, 8, 0) == 0, "CLIP flag not visible to FCAND in the next cycle");
    expect(dataWord(unit, 9, 0) == 1, "CLIP flag visible to FCAND after the FMAC latency");
    // Overflow saturates to the largest float and sets O.
    Vu1 big;
    setQword(big, 0, 3e38f, 1, 1, 1);
    Program q;
    q.lower(LQ(XYZW, 1, 0, 0));
    q.up(ADD(X, 2, 1, 1));
    q.end(kNopUpper, SQ(X, 2, 1, 0));
    load(big, q);
    big.run(0);
    expect(dataWord(big, 1, 0) == 0x7F7FFFFFu, "overflow saturates to +max (no infinity)");
    expect((big.mac & 0x8000) != 0 && (big.status & 0x8) != 0, "overflow sets MAC Ox and status O");
}

void testXgkick() {
    using namespace vu;
    Vu1 unit;
    Recorder gs;
    unit.setGifSink(&gs);
    const uint64_t tag[2] = {1ull | (1ull << 15) | (1ull << 60), 0xE};
    const uint64_t ad[2] = {0x123456789ull, 0x61};
    std::memcpy(unit.data + 10 * 16, tag, 16);
    std::memcpy(unit.data + 11 * 16, ad, 16);
    Program p;
    p.lower(IADDIU(3, 0, 10));
    p.end(kNopUpper, XGKICK(3));
    load(unit, p);
    unit.run(0);
    expect(gs.packets.size() == 1 && gs.packets[0].path == 1, "XGKICK sends one PATH1 packet");
    expect(!gs.packets.empty() && gs.packets[0].bytes.size() == 32 &&
           std::memcmp(gs.packets[0].bytes.data(), unit.data + 160, 32) == 0, "XGKICK packet is the GIFtag and its data");
}

void testVifUnpack() {
    using namespace vif;
    Vu1 unit;
    gt4::ps2::Vif1 v(unit);
    Stream s;
    // V4-32 at qword 0.
    const uint32_t v4[4] = {1, 2, 3, 4};
    s.add(STCYCL(1, 1)).add(UNPACK(3, 0, 1, 0)).add(v4, 16);
    // V3-32 at qword 1: W takes the next word (here the next code, a NOP = 0).
    const uint32_t v3[3] = {5, 6, 7};
    s.add(UNPACK(2, 0, 1, 1)).add(v3, 12);
    // V2-16 signed at 2 (x y x y), S-8 unsigned at 3.
    const int16_t v2[2] = {-2, 300};
    s.add(UNPACK(1, 1, 1, 2)).add(v2, 4);
    const uint8_t s8[4] = {200, 0, 0, 0};
    s.add(UNPACK(0, 2, 1, 3, false, true)).add(s8, 4);
    // V4-5 (RGBA 5551) at 4.
    const uint16_t v45[2] = {static_cast<uint16_t>(31 | (1 << 5) | (1u << 15)), 0};
    s.add(UNPACK(3, 3, 1, 4)).add(v45, 4);
    // Offset mode with STROW, and a masked unpack (row, column, write-protect) at 5.
    const uint32_t row[4] = {100, 200, 300, 400};
    s.add(STROW()).add(row, 16).add(STMOD(1));
    const uint32_t v4b[4] = {1, 1, 1, 1};
    s.add(UNPACK(3, 0, 1, 5)).add(v4b, 16);
    s.add(STMOD(0));
    const uint32_t col[4] = {7, 8, 9, 10};
    s.add(STCOL()).add(col, 16).add(STMASK()).add(0u | (1u << 2) | (2u << 4) | (3u << 6));   // x data, y row, z col, w protect
    std::memset(unit.data + 6 * 16, 0xEE, 16);
    s.add(UNPACK(3, 0, 1, 6, true)).add(v4b, 16);
    v.feed(s.bytes(), s.size());
    auto word = [&](uint32_t qw, uint32_t c) { return dataWord(unit, qw, c); };
    expect(word(0, 0) == 1 && word(0, 3) == 4, "UNPACK V4-32");
    expect(word(1, 0) == 5 && word(1, 2) == 7, "UNPACK V3-32");
    expect(word(2, 0) == 0xFFFFFFFEu && word(2, 1) == 300 && word(2, 2) == 0xFFFFFFFEu && word(2, 3) == 300, "UNPACK V2-16 signed, x y x y");
    expect(word(3, 0) == 200 && word(3, 3) == 200, "UNPACK S-8 unsigned to every field");
    expect(word(4, 0) == 248 && word(4, 1) == 8 && word(4, 2) == 0 && word(4, 3) == 0x80, "UNPACK V4-5");
    expect(word(5, 0) == 101 && word(5, 3) == 401, "STMOD offset adds the row");
    expect(word(6, 0) == 1 && word(6, 1) == 200 && word(6, 2) == 7 && word(6, 3) == 0xEEEEEEEEu, "STMASK data / row / column / protect");

    // Skipping write (CL 2 > WL 1): every other qword; filling write (CL 1 < WL 2): the second of each
    // pair repeats the next element without consuming it.
    Vu1 skipUnit;
    gt4::ps2::Vif1 skip(skipUnit);
    Stream k;
    const uint32_t two[8] = {1, 1, 1, 1, 2, 2, 2, 2};
    k.add(STCYCL(2, 1)).add(UNPACK(3, 0, 2, 0)).add(two, 32);
    k.add(STCYCL(1, 2)).add(UNPACK(3, 0, 4, 10)).add(two, 32);
    skip.feed(k.bytes(), k.size());
    expect(dataWord(skipUnit, 0, 0) == 1 && dataWord(skipUnit, 1, 0) == 0 && dataWord(skipUnit, 2, 0) == 2, "skipping write");
    expect(dataWord(skipUnit, 10, 0) == 1 && dataWord(skipUnit, 12, 0) == 2, "filling write consumes one element per cycle block");

    // Double buffering: FLG adds TOPS, which flips between BASE and BASE + OFFSET at each MSCAL.
    Vu1 dbUnit;
    gt4::ps2::Vif1 db(dbUnit);
    vu::Program endOnly;
    endOnly.end();
    Stream d;
    d.add(BASE(100)).add(OFFSET(50)).mpg(endOnly, 0);
    d.add(UNPACK(3, 0, 1, 0, false, false, true)).add(v4, 16).add(MSCAL(0));
    d.add(UNPACK(3, 0, 1, 0, false, false, true)).add(v4b, 16).add(MSCAL(0));
    db.feed(d.bytes(), d.size());
    expect(dataWord(dbUnit, 100, 0) == 1 && dataWord(dbUnit, 150, 0) == 1 && dataWord(dbUnit, 150, 1) == 1 &&
           dataWord(dbUnit, 100, 3) == 4, "UNPACK FLG uses TOPS, flipped by MSCAL");
    expect(dbUnit.top == 150, "XTOP sees the buffer of the last MSCAL");

    // A command split across two feeds.
    Vu1 splitUnit;
    gt4::ps2::Vif1 split(splitUnit);
    Stream sp;
    sp.add(UNPACK(3, 0, 1, 3)).add(v4, 16);
    split.feed(sp.bytes(), 8);
    split.feed(sp.bytes() + 8, sp.size() - 8);
    expect(dataWord(splitUnit, 3, 0) == 1 && dataWord(splitUnit, 3, 3) == 4, "UNPACK split across records");
}

void testTransformMicroprogram() {
    // MPG + UNPACK + MSCAL: the VU1 program transforms three vertices and XGKICKs a triangle.
    Recorder gs;
    gt4::ps2::GraphicsStream stream(gs);
    const float xy[3][2] = {{100, 50}, {300, 50}, {100, 250}};
    const vif::Stream s = transformStream(xy, 255, 0, 255);
    Gt4LinkRecord header{GT4_REC_VIF1, static_cast<uint32_t>(s.size()), 1};
    stream.record(header, s.bytes());
    expect(gs.packets.size() == 1 && gs.packets[0].path == 1, "the microprogram kicks one PATH1 packet");
    if (gs.packets.empty()) return;
    const auto& b = gs.packets[0].bytes;
    expect(b.size() == 7 * 16, "GIFtag + 3 x (RGBAQ, XYZ2)");
    if (b.size() < 7 * 16) return;
    uint32_t w[28];
    std::memcpy(w, b.data(), sizeof(w));
    expect(w[4] == 255 && w[5] == 0 && w[6] == 255 && w[7] == 0x80, "RGBAQ from the colour qword");
    expect(w[8] == (100 + 2048) * 16 && w[9] == (50 + 2048) * 16 && w[11] == 0, "vertex 0 transformed to 12.4 with the 2048 offset");
    expect(w[16] == (300 + 2048) * 16 && w[25] == (250 + 2048) * 16, "vertices 1 and 2");
    expect(stream.vifStats().microprograms == 1 && stream.vifStats().runaways == 0, "one microprogram, ended normally");
}

void testDirectAndRecords() {
    Recorder gs;
    gt4::ps2::GraphicsStream stream(gs);
    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> hostRecords;
    stream.setHostWriter([&](uint32_t type, const void* payload, uint32_t size) {
        const auto* p = static_cast<const uint8_t*>(payload);
        hostRecords.emplace_back(type, std::vector<uint8_t>(p, p + size));
    });
    // VIF1 DIRECT -> PATH2.
    vif::Stream s;
    const uint64_t packet[4] = {1ull | (1ull << 15) | (1ull << 60), 0xE, 42, 0x61};
    s.alignForData().add(vif::DIRECT(2)).add(packet, sizeof(packet));
    stream.record({GT4_REC_VIF1, static_cast<uint32_t>(s.size()), 1}, s.bytes());
    expect(gs.packets.size() == 1 && gs.packets[0].path == 2 && gs.packets[0].bytes.size() == 32, "DIRECT goes to PATH2");
    // GIF PATH3 record: u32 path then qwords (unaligned after the path word).
    std::vector<uint8_t> gif(4 + 32);
    const uint32_t three = 3;
    std::memcpy(gif.data(), &three, 4);
    std::memcpy(gif.data() + 4, packet, 32);
    stream.record({GT4_REC_GIF, static_cast<uint32_t>(gif.size()), 2}, gif.data());
    expect(gs.packets.size() == 2 && gs.packets[1].path == 3 && std::memcmp(gs.packets[1].bytes.data(), packet, 32) == 0, "GIF PATH3 record");
    // Privileged register, VSYNC, GS read.
    struct { uint32_t reg, pad; uint64_t value; } priv{0x80, 0, 0x1234};
    stream.record({GT4_REC_GS_PRIV, sizeof(priv), 3}, reinterpret_cast<const uint8_t*>(&priv));
    const uint32_t vs[2] = {1, 77};
    stream.record({GT4_REC_VSYNC, 8, 4}, reinterpret_cast<const uint8_t*>(vs));
    const uint32_t read[2] = {9, 2};
    stream.record({GT4_REC_GS_READ, 8, 5}, reinterpret_cast<const uint8_t*>(read));
    expect(gs.priv.size() == 1 && gs.priv[0].first == 0x80 && gs.priv[0].second == 0x1234, "GS_PRIV record");
    expect(gs.vsyncs.size() == 1 && gs.vsyncs[0] == 1, "VSYNC record");
    expect(hostRecords.size() == 1 && hostRecords[0].first == GT4_REC_GS_IMAGE && hostRecords[0].second.size() == 8 + 32,
           "GS_READ answered by GS_IMAGE {id, qwc, data}");
    if (!hostRecords.empty()) {
        uint32_t id = 0, first = 0;
        std::memcpy(&id, hostRecords[0].second.data(), 4);
        std::memcpy(&first, hostRecords[0].second.data() + 8, 4);
        expect(id == 9 && first == 0xA0000000u, "GS_IMAGE carries the id and the downloaded data");
    }
}

// MSKPATH3: PATH3 data that arrives while VIF1 masks PATH3 waits for the unmask (or the VBlank).
void testPath3Mask() {
    Recorder gs;
    gt4::ps2::GraphicsStream stream(gs);
    auto vifRecord = [&](uint32_t code) {
        const uint32_t words[4] = {0, 0, 0, code};
        stream.record({GT4_REC_VIF1, 16, 1}, reinterpret_cast<const uint8_t*>(words));
    };
    std::vector<uint8_t> gif(4 + 16);
    const uint32_t three = 3;
    const uint64_t tag[2] = {1ull << 15, 0};
    std::memcpy(gif.data(), &three, 4);
    std::memcpy(gif.data() + 4, tag, 16);
    vifRecord(vif::code(0x06, 0, 0x8000));   // mask
    stream.record({GT4_REC_GIF, static_cast<uint32_t>(gif.size()), 2}, gif.data());
    expect(gs.packets.empty(), "PATH3 held while MSKPATH3 masks it");
    vifRecord(vif::code(0x06, 0, 0));        // unmask
    expect(gs.packets.size() == 1 && gs.packets[0].path == 3, "PATH3 released at the unmask");
    vifRecord(vif::code(0x06, 0, 0x8000));
    stream.record({GT4_REC_GIF, static_cast<uint32_t>(gif.size()), 3}, gif.data());
    const uint32_t vs[2] = {0, 1};
    stream.record({GT4_REC_VSYNC, 8, 4}, reinterpret_cast<const uint8_t*>(vs));
    expect(gs.packets.size() == 2 && stream.stats().path3ForcedAtVsync == 1, "PATH3 still masked is released at the VBlank");
}

void testReplay() {
    const std::string path = (std::filesystem::temp_directory_path() / "gt4_ps2gfx_test.cap").string();
    {
        gt4::ps2::CaptureWriter writer(path);
        expect(writer.ok(), "capture file opens");
        const uint32_t three = 3;
        const uint64_t packet[2] = {(1ull << 15), 0};
        for (uint32_t frame = 0; frame < 3; ++frame) {
            writer.write(GT4_REC_GIF, &three, 4, packet, 16);
            const uint32_t vs[2] = {frame & 1, frame};
            writer.write(GT4_REC_VSYNC, vs, 8);
        }
        writer.write(GT4_REC_GIF, &three, 4, packet, 16);   // trailing records without a VBlank
    }
    std::string error;
    auto replay = gt4::ps2::ReplaySource::open(path, error);
    expect(replay != nullptr, "replay opens the capture");
    if (replay) {
        expect(replay->frames() == 3, "replay counts the VBlanks");
        Recorder gs;
        gt4::ps2::GraphicsStream stream(gs);
        int frames = 0;
        while (replay->nextFrame([&](const Gt4LinkRecord& h, const uint8_t* p) { stream.record(h, p); })) ++frames;
        expect(frames == 3 && gs.vsyncs.size() == 3 && gs.packets.size() == 4, "replay delivers every record, one VBlank at a time");
    }
    std::filesystem::remove(path);
}

void testPad() {
    gt4::ps2::PadInput in;
    in.button[gt4::ps2::PadCross] = 1.0f;
    in.button[gt4::ps2::PadR2] = 0.5f;
    in.leftX = -1.0f;
    in.rightY = 1.0f;
    uint8_t out[18];
    gt4::ps2::encodePad(in, out);
    const uint16_t buttons = static_cast<uint16_t>(out[0] | (out[1] << 8));
    expect(buttons == static_cast<uint16_t>(~((1u << 14) | (1u << 9))), "buttons active low (Cross, R2)");
    expect(out[4] == 0x00 && out[5] == 0x80 && out[2] == 0x80 && out[3] == 0xFF, "sticks RX RY LX LY");
    expect(out[6 + 6] == 0xFF && out[6 + 11] == 128 && out[6 + 0] == 0, "pressures (Cross, R2)");
}

// Interpreter speed (printed, not checked): the transform loop over 200 vertices, repeated.
void reportThroughput() {
    struct Null final : gt4::ps2::GifSink { void gif(uint32_t, const uint8_t*, size_t) override {} } sink;
    Vu1 unit;
    unit.setGifSink(&sink);
    load(unit, vu::transformProgram(200));
    const auto start = std::chrono::steady_clock::now();
    uint64_t cycles = 0;
    const int runs = 2000;
    for (int k = 0; k < runs; ++k) cycles += unit.run(0);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("ps2gfx: VU1 interpreter %.1f M instruction pairs/s, %.1f M VU cycles/s (%llu pairs in %.3f s)\n",
                static_cast<double>(unit.instructions) / seconds * 1e-6, static_cast<double>(cycles) / seconds * 1e-6,
                static_cast<unsigned long long>(unit.instructions), seconds);
}

}  // namespace

int main() {
    reportThroughput();
    testArithmetic();
    testMoreInstructions();
    testBranches();
    testFlags();
    testXgkick();
    testVifUnpack();
#if GT4_VU1_INTERPRETER
    testTransformMicroprogram();   // needs MSCAL to run the program
#endif
    testDirectAndRecords();
    testPath3Mask();
    testReplay();
    testPad();
    if (failures) {
        std::fprintf(stderr, "ps2gfx: %d check(s) failed\n", failures);
        return 1;
    }
    std::printf("ps2gfx: all checks passed\n");
    return 0;
}
