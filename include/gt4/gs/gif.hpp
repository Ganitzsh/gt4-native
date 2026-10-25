#pragma once
// GIF packet decoding: the data the game hands to the GIF DMA kicks (docs/PLATFORM.md, "The renderer
// seam"). A packet is a sequence of 128-bit qwords: a GIFtag, then its data, until a tag with EOP set.
// The decoder turns PACKED and REGLIST data into GS register writes and reports IMAGE data as a block of
// qwords for a host-to-local transfer. Field layout from the public PS2 GS documentation.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace gt4::gs {

enum class GifFormat : std::uint8_t { packed = 0, reglist = 1, image = 2, image3 = 3 };

struct GifTag {
    std::uint16_t nloop = 0;  // repetitions of the register list
    bool eop = false;         // last tag of the packet
    bool pre = false;         // write `prim` to PRIM before the data
    std::uint16_t prim = 0;   // 11-bit PRIM value used when `pre`
    GifFormat flg = GifFormat::packed;
    std::uint8_t nreg = 16;   // 1..16 (0 in the tag means 16)
    std::uint64_t regs = 0;   // nreg 4-bit register descriptors, first in the low bits

    std::uint8_t reg(unsigned i) const { return static_cast<std::uint8_t>((regs >> (4 * i)) & 0xF); }
};

// PACKED register descriptors (the 4-bit codes in a GIFtag's REGS field).
enum PackedReg : std::uint8_t {
    PRIM = 0x0, RGBAQ = 0x1, ST = 0x2, UV = 0x3, XYZF2 = 0x4, XYZ2 = 0x5, TEX0_1 = 0x6, TEX0_2 = 0x7,
    CLAMP_1 = 0x8, CLAMP_2 = 0x9, FOG = 0xA, XYZF3 = 0xC, XYZ3 = 0xD, A_D = 0xE, NOP = 0xF,
};

// GS register addresses (the A+D address byte and the REGLIST/PACKED targets).
enum GsReg : std::uint8_t {
    GS_PRIM = 0x00, GS_RGBAQ = 0x01, GS_ST = 0x02, GS_UV = 0x03, GS_XYZF2 = 0x04, GS_XYZ2 = 0x05,
    GS_TEX0_1 = 0x06, GS_TEX0_2 = 0x07, GS_CLAMP_1 = 0x08, GS_CLAMP_2 = 0x09, GS_FOG = 0x0A,
    GS_XYZF3 = 0x0C, GS_XYZ3 = 0x0D,
};

struct GsWrite {
    std::uint8_t reg = 0;     // GS register address
    std::uint64_t value = 0;  // the 64-bit register value
};

struct GifImage {
    std::size_t first_qword = 0;  // index in the packet of the first data qword
    std::size_t qwords = 0;
};

struct GifDecoded {
    std::vector<GsWrite> writes;
    std::vector<GifImage> images;
    std::size_t qwords_used = 0;  // qwords consumed, up to and including the EOP tag's data
    bool complete = false;        // an EOP tag was reached and its data fit in the packet
};

inline GifTag parse_gif_tag(std::uint64_t lo, std::uint64_t hi) {
    GifTag t;
    t.nloop = static_cast<std::uint16_t>(lo & 0x7FFF);
    t.eop = (lo >> 15) & 1;
    t.pre = (lo >> 46) & 1;
    t.prim = static_cast<std::uint16_t>((lo >> 47) & 0x7FF);
    t.flg = static_cast<GifFormat>((lo >> 58) & 3);
    const unsigned nreg = static_cast<unsigned>((lo >> 60) & 0xF);
    t.nreg = static_cast<std::uint8_t>(nreg == 0 ? 16 : nreg);
    t.regs = hi;
    return t;
}

// One PACKED data qword for register descriptor `desc` to the GS write it performs (none for NOP).
// The packing follows the GS manual's PACKED table; Q in RGBAQ comes from the ST qword's third word.
inline bool packed_write(std::uint8_t desc, std::uint64_t lo, std::uint64_t hi, float& q, GsWrite& out) {
    const auto w = [&](int i) -> std::uint32_t { return static_cast<std::uint32_t>(i < 2 ? lo >> (32 * i) : hi >> (32 * (i - 2))); };
    switch (desc) {
    case PRIM:
        out = {GS_PRIM, lo & 0x7FF};
        return true;
    case RGBAQ: {
        std::uint32_t qbits;
        static_assert(sizeof(float) == 4);
        std::memcpy(&qbits, &q, 4);
        out = {GS_RGBAQ, (w(0) & 0xFF) | ((w(1) & 0xFF) << 8) | ((w(2) & 0xFF) << 16) | (std::uint64_t(w(3) & 0xFF) << 24)
                             | (std::uint64_t(qbits) << 32)};
        return true;
    }
    case ST:
        std::memcpy(&q, reinterpret_cast<const char*>(&hi), 4);
        out = {GS_ST, lo};
        return true;
    case UV:
        out = {GS_UV, (w(0) & 0x3FFF) | (std::uint64_t(w(1) & 0x3FFF) << 16)};
        return true;
    case XYZF2:
    case XYZF3: {
        const bool kick = !((hi >> 47) & 1);  // ADC set: write XYZF3 (no drawing kick)
        const std::uint8_t reg = (desc == XYZF2 && kick) ? GS_XYZF2 : GS_XYZF3;
        out = {reg, (w(0) & 0xFFFF) | (std::uint64_t(w(1) & 0xFFFF) << 16) | (std::uint64_t((w(2) >> 4) & 0xFFFFFF) << 32)
                        | (std::uint64_t((w(3) >> 4) & 0xFF) << 56)};
        return true;
    }
    case XYZ2:
    case XYZ3: {
        const bool kick = !((hi >> 47) & 1);
        const std::uint8_t reg = (desc == XYZ2 && kick) ? GS_XYZ2 : GS_XYZ3;
        out = {reg, (w(0) & 0xFFFF) | (std::uint64_t(w(1) & 0xFFFF) << 16) | (std::uint64_t(w(2)) << 32)};
        return true;
    }
    case TEX0_1: case TEX0_2: case CLAMP_1: case CLAMP_2:
        out = {desc, lo};
        return true;
    case FOG:
        out = {GS_FOG, ((hi >> 36) & 0xFF) << 56};
        return true;
    case A_D:
        out = {static_cast<std::uint8_t>(hi & 0xFF), lo};
        return true;
    default:
        return false;  // NOP and the reserved code 0xB
    }
}

// Decode one GIF packet (the qwords a GIF DMA kick sends). Stops after the EOP tag's data, or at the
// end of `qwords` with `complete == false` when the packet is cut short.
inline GifDecoded decode_gif(std::span<const std::uint64_t> words) {
    GifDecoded out;
    const std::size_t qwords = words.size() / 2;
    std::size_t i = 0;
    float q = 1.0f;
    while (i < qwords) {
        const GifTag tag = parse_gif_tag(words[2 * i], words[2 * i + 1]);
        ++i;
        if (tag.pre && tag.flg == GifFormat::packed)
            out.writes.push_back({GS_PRIM, tag.prim});
        q = 1.0f;
        if (tag.flg == GifFormat::packed) {
            const std::size_t need = std::size_t(tag.nloop) * tag.nreg;
            if (i + need > qwords) { out.qwords_used = qwords; return out; }
            for (std::size_t n = 0; n < need; ++n, ++i) {
                GsWrite w;
                if (packed_write(tag.reg(static_cast<unsigned>(n % tag.nreg)), words[2 * i], words[2 * i + 1], q, w))
                    out.writes.push_back(w);
            }
        } else if (tag.flg == GifFormat::reglist) {
            const std::size_t regs = std::size_t(tag.nloop) * tag.nreg;  // one 64-bit word each
            const std::size_t need = (regs + 1) / 2;
            if (i + need > qwords) { out.qwords_used = qwords; return out; }
            for (std::size_t n = 0; n < regs; ++n) {
                const std::uint8_t desc = tag.reg(static_cast<unsigned>(n % tag.nreg));
                if (desc != NOP && desc != A_D)
                    out.writes.push_back({desc, words[2 * i + n]});
            }
            i += need;
        } else {
            if (i + tag.nloop > qwords) { out.qwords_used = qwords; return out; }
            out.images.push_back({i, tag.nloop});
            i += tag.nloop;
        }
        if (tag.eop) {
            out.qwords_used = i;
            out.complete = true;
            return out;
        }
    }
    out.qwords_used = i;
    return out;
}

}  // namespace gt4::gs
