#pragma once
// GS local memory (4 MiB) as the GS lays out pixels: every pixel storage format's page, block and column
// arrangement, the three transfer directions, the CLUT buffer and texture decoding. The native renderer's
// front end keeps it current with the game's uploads, so textures can be decoded the way the GS reads them
// (GT4 uploads textures as 32-bit blocks and reads them as 4- and 8-bit indices). Checked against
// paraLLEl-GS's memory after the same transfers (tests/gs_native.cpp).
#include <cstddef>
#include <cstdint>
#include <vector>

namespace gt4::gsn {

// Pixel storage modes (PSM field values).
enum Psm : uint32_t {
    PSMCT32 = 0x00, PSMCT24 = 0x01, PSMCT16 = 0x02, PSMCT16S = 0x0A,
    PSMT8 = 0x13, PSMT4 = 0x14, PSMT8H = 0x1B, PSMT4HL = 0x24, PSMT4HH = 0x2C,
    PSMZ32 = 0x30, PSMZ24 = 0x31, PSMZ16 = 0x32, PSMZ16S = 0x3A,
};

bool psmKnown(uint32_t psm);
uint32_t psmBits(uint32_t psm);   // bits per pixel in transfers: 32, 24, 16, 8 or 4

// TEXA: how 16 and 24-bit texels get their alpha.
struct Texa {
    uint8_t ta0 = 0, ta1 = 0x80;
    bool aem = false;
};

class GsMemory {
public:
    static constexpr uint32_t kBytes = 4u << 20;

    GsMemory();
    uint8_t* data() { return mem.data(); }
    const uint8_t* data() const { return mem.data(); }

    // Byte address of pixel (x, y) of a buffer at `bp` (in 256-byte blocks) `bw` (in 64-pixel units) wide.
    // For 4-bit formats the address is in nibbles (byte address = nibble >> 1).
    static uint32_t address32(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y);    // CT32/CT24/T8H/T4HL/T4HH: bytes
    static uint32_t addressZ32(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y);   // Z32/Z24
    static uint32_t address16(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y);    // CT16
    static uint32_t address16S(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y);
    static uint32_t addressZ16(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y);
    static uint32_t addressZ16S(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y);
    static uint32_t address8(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y);     // bytes
    static uint32_t address4(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y);     // nibbles

    // A pixel's raw value in its format (CT24: 24 bits; T4: 4 bits; ...), and the reverse.
    uint32_t readPixel(uint32_t psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y) const;
    void writePixel(uint32_t psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y, uint32_t value);

    // An image transfer as BITBLTBUF/TRXPOS/TRXREG describe it.
    struct Transfer {
        uint32_t sbp = 0, sbw = 0, spsm = 0, dbp = 0, dbw = 0, dpsm = 0;
        uint32_t ssax = 0, ssay = 0, dsax = 0, dsay = 0, dir = 0;   // TRXPOS (dir: local-to-local order)
        uint32_t rrw = 0, rrh = 0;
    };
    // HOST -> LOCAL: `bytes` of packed pixel data (continuing where the last call stopped; begin() resets).
    void beginWrite(const Transfer& t);
    void write(const uint8_t* bytes, size_t size);
    // LOCAL -> LOCAL.
    void copy(const Transfer& t);
    // LOCAL -> HOST: the whole rectangle, packed.
    std::vector<uint8_t> read(const Transfer& t) const;

    // The CLUT buffer (1 KiB, 16-bit entries for CT16 palettes): loaded from memory as TEX0 CLD asks.
    struct Tex0 {
        uint32_t tbp = 0, tbw = 0, psm = 0, tw = 0, th = 0, tcc = 0, tfx = 0;
        uint32_t cbp = 0, cpsm = 0, csm = 0, csa = 0, cld = 0;
        static Tex0 decode(uint64_t v);
    };
    // A TEX0 (or TEX2) write: loads the CLUT buffer when CLD says so (CBP0/CBP1 kept for CLD 4 and 5).
    // True when it loaded.
    bool tex0Written(const Tex0& t);
    const uint8_t* clutBuffer() const { return clut; }   // 1 KiB

    // Decodes a w x h region of the texture TEX0 describes (from texel (u0, v0)) into RGBA8 (R in the low
    // byte), with the CLUT buffer for indexed formats, TEXA for 16/24-bit alpha; TCC = 0 keeps the
    // texture's alpha anyway (the shader ignores it).
    void decode(const Tex0& t, const Texa& texa, uint32_t u0, uint32_t v0, uint32_t w, uint32_t h, uint32_t* out) const;
    // The raw indices of a w x h indexed texture (a 4-bit one's nibbles), one byte per texel.
    void indices(const Tex0& t, uint32_t w, uint32_t h, uint8_t* out) const;

private:
    std::vector<uint8_t> mem;
    uint8_t clut[1024] = {};   // the CLUT buffer: 256 x 32-bit, or 512 x 16-bit entries
    uint32_t cbp0 = ~0u, cbp1 = ~0u;
    // The host-to-local transfer in progress.
    Transfer xfer;
    uint32_t xferX = 0, xferY = 0;
    std::vector<uint8_t> xferPartial;   // bytes of an incomplete pixel (24-bit) carried over
    bool xferActive = false;
    void loadClut(const Tex0& t);
    uint32_t clutEntry(uint32_t index, uint32_t cpsm, uint32_t csa, bool four) const;
};

}  // namespace gt4::gsn
