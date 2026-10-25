// Native code for the hand-written MMI assembly of decomp unit 001C0E78 (image processing of the
// movie/photo code). Written from the original instructions (gt4-decomp: tools/match.py asm ADDR):
// lq/sq ignore the low 4 address bits and sd the low 3, as on the EE.
#include "guest.h"

#include <cstdint>
#include <cstring>

namespace {

inline const uint8_t *qword_at(uintptr_t a) { return reinterpret_cast<const uint8_t *>(a & ~uintptr_t{15}); }

}  // namespace

extern "C" {

// 001C1678 (232 bytes): RGBA32 rows to 8-bit luminance (3R + 4G + B) / 8. Each of `rows` rows reads
// width/4 quadwords (4 pixels each) from src and writes 4 bytes per quadword to dst, then zero-fills
// the rest of the destination row, whose stride is width rounded up to 16. Returns the largest
// luminance written (0 when rows is 0).
int gt4_replace_func_001C1678(int dst, int src, int width, int rows) {
    const uint32_t stride = (static_cast<uint32_t>(width) + 15) & ~15u;
    int16_t mx[4] = {0, 0, 0, 0};
    if (rows != 0) {
        const uint32_t n = static_cast<uint32_t>(width) >> 2;
        uint32_t row = static_cast<uint32_t>(dst), s = static_cast<uint32_t>(src);
        uint32_t left = static_cast<uint32_t>(rows);
        do {
            uint32_t o = row;
            for (uint32_t i = n; i != 0; --i) {
                uint32_t w[4];
                std::memcpy(w, qword_at(s), 16);
                s += 16;
                uint8_t lum[4];
                for (int k = 0; k < 4; ++k) {
                    const uint32_t r = w[k] & 0xFF, g = (w[k] & 0xFF00) >> 6, b = (w[k] & 0xFF0000) >> 16;
                    const uint32_t v = (g + (r * 2 + r) + b) >> 3;
                    const int16_t h = static_cast<int16_t>(v & 0xFFFF);
                    if (h > mx[k]) mx[k] = h;
                    lum[k] = static_cast<uint8_t>(v);
                }
                std::memcpy(gt4::rt::guest_ptr(o & ~3u), lum, 4);
                o += 4;
            }
            row += stride;
            while (o != row) {
                std::memset(gt4::rt::guest_ptr(o & ~3u), 0, 4);
                o += 4;
            }
        } while (--left != 0);
    }
    int16_t a = mx[0] > mx[2] ? mx[0] : mx[2];
    int16_t b = mx[1] > mx[3] ? mx[1] : mx[3];
    if (b > a) a = b;
    return static_cast<uint16_t>(a);
}

// 001C1A50 (296 bytes): halves an 8-bit image: each output byte is the sum of a 2x2 block of the
// source / 4. dst rows are (w + 7) & ~7 bytes written in 8-byte stores, stride (w + 15) & ~15;
// source stride (sw + 15) & ~15. h - (sh & 1) rows are made from row pairs; when sh is odd, one more
// row from the last source row alone (its horizontal pairs / 4).
void gt4_replace_func_001C1A50(uint32_t dst, uint32_t w, uint32_t h, uint32_t src, uint32_t sw, uint32_t sh) {
    const uint32_t sstride = (sw + 15) & ~15u;
    const uint32_t dstride = (w + 15) & ~15u;
    const uint32_t chunks = ((w + 7) & ~7u) >> 3;
    const uint32_t odd = sh & 1;
    uint32_t rows = h - odd;
    uint32_t drow = dst;
    if (rows != 0) {
        do {
            uint32_t o = drow, r0 = src, r1 = src + sstride;
            for (uint32_t i = chunks; i != 0; --i) {
                uint8_t a[16], b[16], out[8];
                std::memcpy(a, qword_at(r0), 16);
                std::memcpy(b, qword_at(r1), 16);
                r0 += 16;
                r1 += 16;
                for (int k = 0; k < 8; ++k) {
                    const uint32_t s = (a[2 * k] + a[2 * k + 1]) + (b[2 * k] + b[2 * k + 1]);
                    out[k] = static_cast<uint8_t>(s >> 2);
                }
                std::memcpy(gt4::rt::guest_ptr(o & ~7u), out, 8);
                o += 8;
            }
            drow += dstride;
            src += sstride * 2;
        } while (--rows != 0);
    }
    if (odd) {
        uint32_t o = drow;
        for (uint32_t i = chunks; i != 0; --i) {
            uint8_t a[16], out[8];
            std::memcpy(a, qword_at(src), 16);
            src += 16;
            for (int k = 0; k < 8; ++k) out[k] = static_cast<uint8_t>((a[2 * k] + a[2 * k + 1]) >> 2);
            std::memcpy(gt4::rt::guest_ptr(o & ~7u), out, 8);
            o += 8;
        }
    }
}

}  // extern "C"
