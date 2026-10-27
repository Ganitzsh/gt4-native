// GS local memory (see gs_memory.h). The page/block/column layouts are those of the GS documentation:
// a page is 8 KiB (32 blocks of 256 bytes), its pixels are arranged in blocks (block tables), and a block's
// pixels in four columns (column tables).
#include "gs_memory.h"

#include <algorithm>
#include <cstring>

namespace gt4::gsn {

namespace {

// Block number at block (row, column) of a page. The Z formats use the colour formats' layouts with bits 3
// and 4 of the block address flipped, after the base is added (paraLLEl-GS and the GS agree on that).
constexpr uint8_t kBlock32[4][8] = {
    {0, 1, 4, 5, 16, 17, 20, 21}, {2, 3, 6, 7, 18, 19, 22, 23}, {8, 9, 12, 13, 24, 25, 28, 29}, {10, 11, 14, 15, 26, 27, 30, 31}};

constexpr uint8_t kBlock16[8][4] = {
    {0, 2, 8, 10}, {1, 3, 9, 11}, {4, 6, 12, 14}, {5, 7, 13, 15}, {16, 18, 24, 26}, {17, 19, 25, 27}, {20, 22, 28, 30}, {21, 23, 29, 31}};
constexpr uint8_t kBlock16S[8][4] = {
    {0, 2, 16, 18}, {1, 3, 17, 19}, {8, 10, 24, 26}, {9, 11, 25, 27}, {4, 6, 20, 22}, {5, 7, 21, 23}, {12, 14, 28, 30}, {13, 15, 29, 31}};


constexpr uint8_t kBlock8[4][8] = {
    {0, 1, 4, 5, 16, 17, 20, 21}, {2, 3, 6, 7, 18, 19, 22, 23}, {8, 9, 12, 13, 24, 25, 28, 29}, {10, 11, 14, 15, 26, 27, 30, 31}};
constexpr uint8_t kBlock4[8][4] = {
    {0, 2, 8, 10}, {1, 3, 9, 11}, {4, 6, 12, 14}, {5, 7, 13, 15}, {16, 18, 24, 26}, {17, 19, 25, 27}, {20, 22, 28, 30}, {21, 23, 29, 31}};

// Pixel (row, column) of a block: its index in the block, in pixels of the format.
constexpr uint8_t kColumn32[8][8] = {
    {0, 1, 4, 5, 8, 9, 12, 13},       {2, 3, 6, 7, 10, 11, 14, 15},     {16, 17, 20, 21, 24, 25, 28, 29}, {18, 19, 22, 23, 26, 27, 30, 31},
    {32, 33, 36, 37, 40, 41, 44, 45}, {34, 35, 38, 39, 42, 43, 46, 47}, {48, 49, 52, 53, 56, 57, 60, 61}, {50, 51, 54, 55, 58, 59, 62, 63}};
constexpr uint8_t kColumn16[8][16] = {
    {0, 2, 8, 10, 16, 18, 24, 26, 1, 3, 9, 11, 17, 19, 25, 27},
    {4, 6, 12, 14, 20, 22, 28, 30, 5, 7, 13, 15, 21, 23, 29, 31},
    {32, 34, 40, 42, 48, 50, 56, 58, 33, 35, 41, 43, 49, 51, 57, 59},
    {36, 38, 44, 46, 52, 54, 60, 62, 37, 39, 45, 47, 53, 55, 61, 63},
    {64, 66, 72, 74, 80, 82, 88, 90, 65, 67, 73, 75, 81, 83, 89, 91},
    {68, 70, 76, 78, 84, 86, 92, 94, 69, 71, 77, 79, 85, 87, 93, 95},
    {96, 98, 104, 106, 112, 114, 120, 122, 97, 99, 105, 107, 113, 115, 121, 123},
    {100, 102, 108, 110, 116, 118, 124, 126, 101, 103, 109, 111, 117, 119, 125, 127}};
constexpr uint8_t kColumn8[16][16] = {
    {0, 4, 16, 20, 32, 36, 48, 52, 2, 6, 18, 22, 34, 38, 50, 54},
    {8, 12, 24, 28, 40, 44, 56, 60, 10, 14, 26, 30, 42, 46, 58, 62},
    {33, 37, 49, 53, 1, 5, 17, 21, 35, 39, 51, 55, 3, 7, 19, 23},
    {41, 45, 57, 61, 9, 13, 25, 29, 43, 47, 59, 63, 11, 15, 27, 31},
    {96, 100, 112, 116, 64, 68, 80, 84, 98, 102, 114, 118, 66, 70, 82, 86},
    {104, 108, 120, 124, 72, 76, 88, 92, 106, 110, 122, 126, 74, 78, 90, 94},
    {65, 69, 81, 85, 97, 101, 113, 117, 67, 71, 83, 87, 99, 103, 115, 119},
    {73, 77, 89, 93, 105, 109, 121, 125, 75, 79, 91, 95, 107, 111, 123, 127},
    {128, 132, 144, 148, 160, 164, 176, 180, 130, 134, 146, 150, 162, 166, 178, 182},
    {136, 140, 152, 156, 168, 172, 184, 188, 138, 142, 154, 158, 170, 174, 186, 190},
    {161, 165, 177, 181, 129, 133, 145, 149, 163, 167, 179, 183, 131, 135, 147, 151},
    {169, 173, 185, 189, 137, 141, 153, 157, 171, 175, 187, 191, 139, 143, 155, 159},
    {224, 228, 240, 244, 192, 196, 208, 212, 226, 230, 242, 246, 194, 198, 210, 214},
    {232, 236, 248, 252, 200, 204, 216, 220, 234, 238, 250, 254, 202, 206, 218, 222},
    {193, 197, 209, 213, 225, 229, 241, 245, 195, 199, 211, 215, 227, 231, 243, 247},
    {201, 205, 217, 221, 233, 237, 249, 253, 203, 207, 219, 223, 235, 239, 251, 255}};
constexpr uint16_t kColumn4[16][32] = {
    {0, 8, 32, 40, 64, 72, 96, 104, 2, 10, 34, 42, 66, 74, 98, 106, 4, 12, 36, 44, 68, 76, 100, 108, 6, 14, 38, 46, 70, 78, 102, 110},
    {16, 24, 48, 56, 80, 88, 112, 120, 18, 26, 50, 58, 82, 90, 114, 122, 20, 28, 52, 60, 84, 92, 116, 124, 22, 30, 54, 62, 86, 94, 118, 126},
    {65, 73, 97, 105, 1, 9, 33, 41, 67, 75, 99, 107, 3, 11, 35, 43, 69, 77, 101, 109, 5, 13, 37, 45, 71, 79, 103, 111, 7, 15, 39, 47},
    {81, 89, 113, 121, 17, 25, 49, 57, 83, 91, 115, 123, 19, 27, 51, 59, 85, 93, 117, 125, 21, 29, 53, 61, 87, 95, 119, 127, 23, 31, 55, 63},
    {192, 200, 224, 232, 128, 136, 160, 168, 194, 202, 226, 234, 130, 138, 162, 170, 196, 204, 228, 236, 132, 140, 164, 172, 198, 206, 230, 238, 134, 142, 166, 174},
    {208, 216, 240, 248, 144, 152, 176, 184, 210, 218, 242, 250, 146, 154, 178, 186, 212, 220, 244, 252, 148, 156, 180, 188, 214, 222, 246, 254, 150, 158, 182, 190},
    {129, 137, 161, 169, 193, 201, 225, 233, 131, 139, 163, 171, 195, 203, 227, 235, 133, 141, 165, 173, 197, 205, 229, 237, 135, 143, 167, 175, 199, 207, 231, 239},
    {145, 153, 177, 185, 209, 217, 241, 249, 147, 155, 179, 187, 211, 219, 243, 251, 149, 157, 181, 189, 213, 221, 245, 253, 151, 159, 183, 191, 215, 223, 247, 255},
    {256, 264, 288, 296, 320, 328, 352, 360, 258, 266, 290, 298, 322, 330, 354, 362, 260, 268, 292, 300, 324, 332, 356, 364, 262, 270, 294, 302, 326, 334, 358, 366},
    {272, 280, 304, 312, 336, 344, 368, 376, 274, 282, 306, 314, 338, 346, 370, 378, 276, 284, 308, 316, 340, 348, 372, 380, 278, 286, 310, 318, 342, 350, 374, 382},
    {321, 329, 353, 361, 257, 265, 289, 297, 323, 331, 355, 363, 259, 267, 291, 299, 325, 333, 357, 365, 261, 269, 293, 301, 327, 335, 359, 367, 263, 271, 295, 303},
    {337, 345, 369, 377, 273, 281, 305, 313, 339, 347, 371, 379, 275, 283, 307, 315, 341, 349, 373, 381, 277, 285, 309, 317, 343, 351, 375, 383, 279, 287, 311, 319},
    {448, 456, 480, 488, 384, 392, 416, 424, 450, 458, 482, 490, 386, 394, 418, 426, 452, 460, 484, 492, 388, 396, 420, 428, 454, 462, 486, 494, 390, 398, 422, 430},
    {464, 472, 496, 504, 400, 408, 432, 440, 466, 474, 498, 506, 402, 410, 434, 442, 468, 476, 500, 508, 404, 412, 436, 444, 470, 478, 502, 510, 406, 414, 438, 446},
    {385, 393, 417, 425, 449, 457, 481, 489, 387, 395, 419, 427, 451, 459, 483, 491, 389, 397, 421, 429, 453, 461, 485, 493, 391, 399, 423, 431, 455, 463, 487, 495},
    {401, 409, 433, 441, 465, 473, 497, 505, 403, 411, 435, 443, 467, 475, 499, 507, 405, 413, 437, 445, 469, 477, 501, 509, 407, 415, 439, 447, 471, 479, 503, 511}};

constexpr uint32_t kMask = GsMemory::kBytes - 1;

bool indexed(uint32_t psm) { return psm == PSMT8 || psm == PSMT4 || psm == PSMT8H || psm == PSMT4HL || psm == PSMT4HH; }

}  // namespace

bool psmKnown(uint32_t psm) {
    switch (psm) {
    case PSMCT32: case PSMCT24: case PSMCT16: case PSMCT16S: case PSMT8: case PSMT4: case PSMT8H: case PSMT4HL: case PSMT4HH:
    case PSMZ32: case PSMZ24: case PSMZ16: case PSMZ16S: return true;
    default: return false;
    }
}

uint32_t psmBits(uint32_t psm) {
    switch (psm) {
    case PSMCT32: case PSMZ32: return 32;
    case PSMCT24: case PSMZ24: return 24;
    case PSMCT16: case PSMCT16S: case PSMZ16: case PSMZ16S: return 16;
    case PSMT8: case PSMT8H: return 8;
    default: return 4;
    }
}

GsMemory::GsMemory() : mem(kBytes, 0) {}

uint32_t GsMemory::address32(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y) {
    const uint32_t page = (x >> 6) + (y >> 5) * bw;
    const uint32_t block = bp + page * 32 + kBlock32[(y >> 3) & 3][(x >> 3) & 7];
    return ((block * 64 + kColumn32[y & 7][x & 7]) * 4) & kMask;
}
uint32_t GsMemory::addressZ32(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y) {
    const uint32_t page = (x >> 6) + (y >> 5) * bw;
    const uint32_t block = (bp + page * 32 + kBlock32[(y >> 3) & 3][(x >> 3) & 7]) ^ 24;
    return ((block * 64 + kColumn32[y & 7][x & 7]) * 4) & kMask;
}
uint32_t GsMemory::address16(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y) {
    const uint32_t page = (x >> 6) + (y >> 6) * bw;
    const uint32_t block = bp + page * 32 + kBlock16[(y >> 3) & 7][(x >> 4) & 3];
    return ((block * 128 + kColumn16[y & 7][x & 15]) * 2) & kMask;
}
uint32_t GsMemory::address16S(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y) {
    const uint32_t page = (x >> 6) + (y >> 6) * bw;
    const uint32_t block = bp + page * 32 + kBlock16S[(y >> 3) & 7][(x >> 4) & 3];
    return ((block * 128 + kColumn16[y & 7][x & 15]) * 2) & kMask;
}
uint32_t GsMemory::addressZ16(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y) {
    const uint32_t page = (x >> 6) + (y >> 6) * bw;
    const uint32_t block = (bp + page * 32 + kBlock16[(y >> 3) & 7][(x >> 4) & 3]) ^ 24;
    return ((block * 128 + kColumn16[y & 7][x & 15]) * 2) & kMask;
}
uint32_t GsMemory::addressZ16S(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y) {
    const uint32_t page = (x >> 6) + (y >> 6) * bw;
    const uint32_t block = (bp + page * 32 + kBlock16S[(y >> 3) & 7][(x >> 4) & 3]) ^ 24;
    return ((block * 128 + kColumn16[y & 7][x & 15]) * 2) & kMask;
}
uint32_t GsMemory::address8(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y) {
    const uint32_t page = (x >> 7) + (y >> 6) * (bw >> 1);
    const uint32_t block = bp + page * 32 + kBlock8[(y >> 4) & 3][(x >> 4) & 7];
    return (block * 256 + kColumn8[y & 15][x & 15]) & kMask;
}
uint32_t GsMemory::address4(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y) {
    const uint32_t page = (x >> 7) + (y >> 7) * (bw >> 1);
    const uint32_t block = bp + page * 32 + kBlock4[(y >> 4) & 7][(x >> 5) & 3];
    return (block * 512 + kColumn4[y & 15][x & 31]) & ((kMask << 1) | 1);
}

uint32_t GsMemory::readPixel(uint32_t psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y) const {
    x &= 2047;
    y &= 2047;
    auto word = [&](uint32_t a) { uint32_t v; std::memcpy(&v, &mem[a], 4); return v; };
    auto half = [&](uint32_t a) { uint16_t v; std::memcpy(&v, &mem[a], 2); return static_cast<uint32_t>(v); };
    switch (psm) {
    case PSMCT32: return word(address32(bp, bw, x, y));
    case PSMCT24: return word(address32(bp, bw, x, y)) & 0xFFFFFF;
    case PSMZ32: return word(addressZ32(bp, bw, x, y));
    case PSMZ24: return word(addressZ32(bp, bw, x, y)) & 0xFFFFFF;
    case PSMCT16: return half(address16(bp, bw, x, y));
    case PSMCT16S: return half(address16S(bp, bw, x, y));
    case PSMZ16: return half(addressZ16(bp, bw, x, y));
    case PSMZ16S: return half(addressZ16S(bp, bw, x, y));
    case PSMT8: return mem[address8(bp, bw, x, y)];
    case PSMT4: {
        const uint32_t n = address4(bp, bw, x, y);
        return (mem[n >> 1] >> ((n & 1) * 4)) & 15;
    }
    case PSMT8H: return word(address32(bp, bw, x, y)) >> 24;
    case PSMT4HL: return (word(address32(bp, bw, x, y)) >> 24) & 15;
    case PSMT4HH: return word(address32(bp, bw, x, y)) >> 28;
    default: return 0;
    }
}

void GsMemory::writePixel(uint32_t psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y, uint32_t value) {
    x &= 2047;
    y &= 2047;
    auto word = [&](uint32_t a, uint32_t v, uint32_t keep) {
        uint32_t old;
        std::memcpy(&old, &mem[a], 4);
        v = (old & keep) | (v & ~keep);
        std::memcpy(&mem[a], &v, 4);
    };
    auto half = [&](uint32_t a, uint32_t v) { const uint16_t h = static_cast<uint16_t>(v); std::memcpy(&mem[a], &h, 2); };
    switch (psm) {
    case PSMCT32: word(address32(bp, bw, x, y), value, 0); break;
    case PSMCT24: word(address32(bp, bw, x, y), value, 0xFF000000u); break;
    case PSMZ32: word(addressZ32(bp, bw, x, y), value, 0); break;
    case PSMZ24: word(addressZ32(bp, bw, x, y), value, 0xFF000000u); break;
    case PSMCT16: half(address16(bp, bw, x, y), value); break;
    case PSMCT16S: half(address16S(bp, bw, x, y), value); break;
    case PSMZ16: half(addressZ16(bp, bw, x, y), value); break;
    case PSMZ16S: half(addressZ16S(bp, bw, x, y), value); break;
    case PSMT8: mem[address8(bp, bw, x, y)] = static_cast<uint8_t>(value); break;
    case PSMT4: {
        const uint32_t n = address4(bp, bw, x, y);
        uint8_t& b = mem[n >> 1];
        b = (n & 1) ? static_cast<uint8_t>((b & 0x0F) | ((value & 15) << 4)) : static_cast<uint8_t>((b & 0xF0) | (value & 15));
        break;
    }
    case PSMT8H: word(address32(bp, bw, x, y), value << 24, 0x00FFFFFFu); break;
    case PSMT4HL: word(address32(bp, bw, x, y), (value & 15) << 24, 0xF0FFFFFFu); break;
    case PSMT4HH: word(address32(bp, bw, x, y), (value & 15) << 28, 0x0FFFFFFFu); break;
    default: break;
    }
}

void GsMemory::beginWrite(const Transfer& t) {
    xfer = t;
    xferX = xferY = 0;
    xferPartial.clear();
    xferActive = t.rrw && t.rrh && psmKnown(t.dpsm);
}

void GsMemory::write(const uint8_t* bytes, size_t size) {
    if (!xferActive) return;
    const uint32_t bits = psmBits(xfer.dpsm);
    auto next = [&](uint32_t value) {
        if (xferY >= xfer.rrh) return;
        writePixel(xfer.dpsm, xfer.dbp, xfer.dbw, xfer.dsax + xferX, xfer.dsay + xferY, value);
        if (++xferX == xfer.rrw) {
            xferX = 0;
            ++xferY;
        }
    };
    if (bits == 4) {
        for (size_t k = 0; k < size; ++k) {
            next(bytes[k] & 15);
            next(bytes[k] >> 4);
        }
    } else {
        const uint32_t step = bits / 8;
        size_t k = 0;
        if (!xferPartial.empty()) {   // a 24-bit pixel split across calls
            while (xferPartial.size() < step && k < size) xferPartial.push_back(bytes[k++]);
            if (xferPartial.size() < step) return;
            uint32_t v = 0;
            std::memcpy(&v, xferPartial.data(), step);
            next(v);
            xferPartial.clear();
        }
        for (; k + step <= size; k += step) {
            uint32_t v = 0;
            std::memcpy(&v, bytes + k, step);
            next(v);
        }
        while (k < size) xferPartial.push_back(bytes[k++]);
    }
    if (xferY >= xfer.rrh) xferActive = false;
}

void GsMemory::copy(const Transfer& t) {
    if (!psmKnown(t.spsm) || !psmKnown(t.dpsm)) return;
    // DIR: 0 top-left first, 1 bottom-left, 2 top-right, 3 bottom-right (matters for overlapping copies).
    const bool up = t.dir == 1 || t.dir == 3, left = t.dir == 2 || t.dir == 3;
    for (uint32_t j = 0; j < t.rrh; ++j) {
        const uint32_t y = up ? t.rrh - 1 - j : j;
        for (uint32_t i = 0; i < t.rrw; ++i) {
            const uint32_t x = left ? t.rrw - 1 - i : i;
            writePixel(t.dpsm, t.dbp, t.dbw, t.dsax + x, t.dsay + y, readPixel(t.spsm, t.sbp, t.sbw, t.ssax + x, t.ssay + y));
        }
    }
}

std::vector<uint8_t> GsMemory::read(const Transfer& t) const {
    std::vector<uint8_t> out;
    if (!psmKnown(t.spsm)) return out;
    const uint32_t bits = psmBits(t.spsm);
    out.reserve(size_t(t.rrw) * t.rrh * bits / 8 + 1);
    bool low = true;
    for (uint32_t y = 0; y < t.rrh; ++y)
        for (uint32_t x = 0; x < t.rrw; ++x) {
            const uint32_t v = readPixel(t.spsm, t.sbp, t.sbw, t.ssax + x, t.ssay + y);
            if (bits == 4) {
                if (low) out.push_back(static_cast<uint8_t>(v & 15));
                else out.back() = static_cast<uint8_t>(out.back() | ((v & 15) << 4));
                low = !low;
            } else {
                for (uint32_t b = 0; b < bits / 8; ++b) out.push_back(static_cast<uint8_t>(v >> (8 * b)));
            }
        }
    return out;
}

GsMemory::Tex0 GsMemory::Tex0::decode(uint64_t v) {
    Tex0 t;
    t.tbp = v & 0x3FFF;
    t.tbw = (v >> 14) & 0x3F;
    t.psm = (v >> 20) & 0x3F;
    t.tw = (v >> 26) & 0xF;
    t.th = (v >> 30) & 0xF;
    t.tcc = (v >> 34) & 1;
    t.tfx = (v >> 35) & 3;
    t.cbp = (v >> 37) & 0x3FFF;
    t.cpsm = (v >> 51) & 0xF;
    t.csm = (v >> 55) & 1;
    t.csa = (v >> 56) & 0x1F;
    t.cld = (v >> 61) & 7;
    return t;
}

void GsMemory::tex0Written(const Tex0& t) {
    if (!indexed(t.psm)) return;
    bool load = false;
    switch (t.cld) {
    case 1: load = true; break;
    case 2: load = true; cbp0 = t.cbp; break;
    case 3: load = true; cbp1 = t.cbp; break;
    case 4: load = t.cbp != cbp0; cbp0 = t.cbp; break;
    case 5: load = t.cbp != cbp1; cbp1 = t.cbp; break;
    default: break;
    }
    if (load) loadClut(t);
}

// The CLUT buffer holds 512 halfwords: a CT32 entry i keeps its low half at i and its high half at 256 + i;
// a CT16 entry i is halfword i. 4-bit textures load 16 entries at CSA * 16, 8-bit ones all 256 at 0.
// CSM1 stores a 256-colour palette as 16 x 16 pixels with entries 8-15 and 16-23 of each 32 swapped, a
// 16-colour one as 8 x 2 pixels.
void GsMemory::loadClut(const Tex0& t) {
    const bool four = t.psm == PSMT4 || t.psm == PSMT4HL || t.psm == PSMT4HH;
    const uint32_t count = four ? 16 : 256;
    const bool ct32 = t.cpsm == PSMCT32 || t.cpsm == PSMCT24;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t x, y;
        if (four) {
            x = i & 7;
            y = i >> 3;
        } else {
            const uint32_t p = (i & ~0x18u) | ((i & 0x08u) << 1) | ((i & 0x10u) >> 1);
            x = p & 15;
            y = p >> 4;
        }
        const uint32_t v = readPixel(t.cpsm == PSMCT24 ? PSMCT32 : t.cpsm, t.cbp, 1, x, y);
        uint16_t* buf = reinterpret_cast<uint16_t*>(clut);
        if (ct32) {
            const uint32_t e = four ? ((t.csa & 15) * 16 + i) & 255 : i;
            buf[e] = static_cast<uint16_t>(v);
            buf[256 + e] = static_cast<uint16_t>(v >> 16);
        } else {
            const uint32_t e = four ? (t.csa * 16 + i) & 511 : i;
            buf[e] = static_cast<uint16_t>(v);
        }
    }
}

uint32_t GsMemory::clutEntry(uint32_t index, uint32_t cpsm, uint32_t csa, bool four) const {
    const uint16_t* buf = reinterpret_cast<const uint16_t*>(clut);
    if (cpsm == PSMCT32 || cpsm == PSMCT24) {
        const uint32_t e = four ? ((csa & 15) * 16 + index) & 255 : index & 255;
        return buf[e] | (static_cast<uint32_t>(buf[256 + e]) << 16);
    }
    const uint32_t e = four ? (csa * 16 + index) & 511 : index & 255;
    return buf[e];
}

namespace {
uint32_t expand16(uint32_t c, const Texa& texa) {
    const uint32_t r = (c & 31) << 3, g = ((c >> 5) & 31) << 3, b = ((c >> 10) & 31) << 3;
    uint32_t a = (c & 0x8000) ? texa.ta1 : texa.ta0;
    if (texa.aem && !(c & 0x7FFF) && !(c & 0x8000)) a = 0;
    return r | (g << 8) | (b << 16) | (a << 24);
}
}  // namespace

void GsMemory::decode(const Tex0& t, const Texa& texa, uint32_t u0, uint32_t v0, uint32_t w, uint32_t h, uint32_t* out) const {
    const bool four = t.psm == PSMT4 || t.psm == PSMT4HL || t.psm == PSMT4HH;
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            const uint32_t raw = readPixel(t.psm, t.tbp, t.tbw, u0 + x, v0 + y);
            uint32_t c;
            switch (t.psm) {
            case PSMCT32: case PSMZ32: c = raw; break;
            case PSMCT24: case PSMZ24: {
                uint32_t a = texa.ta0;
                if (texa.aem && !(raw & 0xFFFFFF)) a = 0;
                c = (raw & 0xFFFFFF) | (a << 24);
                break;
            }
            case PSMCT16: case PSMCT16S: case PSMZ16: case PSMZ16S: c = expand16(raw, texa); break;
            default: {
                const uint32_t e = clutEntry(raw, t.cpsm, t.csa, four);
                c = (t.cpsm == PSMCT32 || t.cpsm == PSMCT24) ? e : expand16(e, texa);
                break;
            }
            }
            out[y * w + x] = c;
        }
}

}  // namespace gt4::gsn
