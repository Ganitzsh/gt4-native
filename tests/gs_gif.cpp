#include "gt4/gs/gif.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

using namespace gt4::gs;

static int failures = 0;
static void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

static std::uint64_t tag_lo(unsigned nloop, bool eop, bool pre, unsigned prim, unsigned flg, unsigned nreg) {
    return std::uint64_t(nloop) | (std::uint64_t(eop) << 15) | (std::uint64_t(pre) << 46) | (std::uint64_t(prim) << 47)
           | (std::uint64_t(flg) << 58) | (std::uint64_t(nreg & 0xF) << 60);
}

int main() {
    // A+D: two register writes (FRAME_1 = 0x4C, TEST_1 = 0x47), EOP.
    {
        std::vector<std::uint64_t> p = {tag_lo(2, true, false, 0, 0, 1), A_D,
                                        0x0000000000010000ull, 0x4C,
                                        0x0000000000030000ull, 0x47};
        const GifDecoded d = decode_gif(p);
        check(d.complete && d.qwords_used == 3, "A+D packet complete");
        check(d.writes.size() == 2 && d.writes[0].reg == 0x4C && d.writes[0].value == 0x10000 && d.writes[1].reg == 0x47,
              "A+D writes decoded in order");
    }
    // PACKED with PRE: one sprite vertex pair as RGBAQ, XYZ2, XYZ2.
    {
        const std::uint64_t regs = RGBAQ | (XYZ2 << 4) | (XYZ2 << 8);
        std::vector<std::uint64_t> p = {tag_lo(1, true, true, 6, 0, 3), regs,
                                        0x0000002000000010ull, 0x0000008000000040ull,  // r=0x10 g=0x20 b=0x40 a=0x80
                                        0x0000001000000100ull, 0x0000000000000005ull,  // x=0x100 y=0x10 z=5
                                        0x0000002000000200ull, 0x0000000000000006ull};
        const GifDecoded d = decode_gif(p);
        check(d.complete, "PACKED packet complete");
        check(d.writes.size() == 4 && d.writes[0].reg == GS_PRIM && d.writes[0].value == 6, "PRE writes PRIM first");
        check(d.writes[1].reg == GS_RGBAQ && (d.writes[1].value & 0xFFFFFFFF) == 0x80402010, "RGBAQ packs the bytes");
        float q;
        const std::uint32_t qbits = static_cast<std::uint32_t>(d.writes[1].value >> 32);
        std::memcpy(&q, &qbits, 4);
        check(q == 1.0f, "RGBAQ carries Q = 1 when no ST came first");
        check(d.writes[2].reg == GS_XYZ2 && d.writes[2].value == ((5ull << 32) | (0x10ull << 16) | 0x100), "XYZ2 packs x, y, z");
    }
    // XYZ2 with ADC set writes XYZ3 (no drawing kick).
    {
        std::vector<std::uint64_t> p = {tag_lo(1, true, false, 0, 0, 1), XYZ2, 0x0000000100000001ull, 1ull << 47};
        const GifDecoded d = decode_gif(p);
        check(d.writes.size() == 1 && d.writes[0].reg == GS_XYZ3, "ADC turns XYZ2 into XYZ3");
    }
    // REGLIST: two 64-bit register words in one qword.
    {
        std::vector<std::uint64_t> p = {tag_lo(1, true, false, 0, 1, 2), UV | (ST << 4), 0x11ull, 0x22ull};
        const GifDecoded d = decode_gif(p);
        check(d.complete && d.writes.size() == 2 && d.writes[0].reg == UV && d.writes[1].value == 0x22, "REGLIST words");
    }
    // IMAGE followed by a second tag: the image block is reported, decoding continues to EOP.
    {
        std::vector<std::uint64_t> p = {tag_lo(2, false, false, 0, 2, 0), 0, 1, 2, 3, 4,
                                        tag_lo(1, true, false, 0, 0, 1), A_D, 0x0ull, 0x54};
        const GifDecoded d = decode_gif(p);
        check(d.images.size() == 1 && d.images[0].first_qword == 1 && d.images[0].qwords == 2, "IMAGE block located");
        check(d.complete && d.writes.size() == 1 && d.writes[0].reg == 0x54, "tag after IMAGE decoded");
    }
    // A packet cut short is reported incomplete, not read past its end.
    {
        std::vector<std::uint64_t> p = {tag_lo(4, true, false, 0, 0, 1), A_D, 0x1ull, 0x4C};
        const GifDecoded d = decode_gif(p);
        check(!d.complete, "truncated packet is incomplete");
    }
    return failures == 0 ? 0 : 1;
}
