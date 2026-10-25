// Native code for the hand-written assembly of decomp unit 0057A128 (Sony SDK library code): the
// DEFLATE decoder 0058A280-0058ACB0 (its entry 0058A4A8 is called by the gzip reader 0058AE10) and
// its packed memcpy 0058ACB0. Written from the original instructions (gt4-decomp:
// tools/match.py asm ADDR).
#include "guest.h"
#include "log.h"

#include <cstdint>
#include <cstring>
#include <utility>

using gt4::rt::guest_ptr;

namespace {

// ---- 0058ACB0: the packed memcpy ------------------------------------------------------------
// A forward copy that is also the LZ77 match copy (source below the destination, overlapping):
// distances under 16 copy byte by byte, distance 1 fills. Like the original it writes past the end
// of a short copy (under 16 bytes at distance 8 or more: 16 bytes are stored, or 8 when the length
// is under 8 at distance 8-15) and rounds a distance-1 fill up to 16 bytes; those bytes are copies
// of the source (or of the fill byte).
void byte_loop(uint8_t *d, const uint8_t *s, uint32_t n) {
    // the original decrements first and stops at zero: n == 0 would run on; it never reaches here
    // with 0 (callers pass at least 1 byte on this path), one byte is copied in that case
    do {
        *d++ = *s++;
    } while (n != 0 && --n != 0);
}

void packed_memcpy(uint32_t dst, uint32_t src, uint32_t len) {
    auto *d = static_cast<uint8_t *>(guest_ptr(dst));
    auto *s = static_cast<const uint8_t *>(guest_ptr(src));
    const uint32_t dist = dst - src;
    if (len < 16) {
        uint8_t a[8], b[8];
        std::memcpy(a, s, 8);
        if (dist < 8) return byte_loop(d, s, len);
        std::memcpy(b, s + 8, 8);
        if (dist < 16) {
            std::memcpy(d, a, 8);
            if (len < 8) return;
            std::memcpy(b, s + 8, 8);  // reloaded after the first store
            std::memcpy(d + 8, b, 8);
            return;
        }
        std::memcpy(d, a, 8);
        std::memcpy(d + 8, b, 8);
        return;
    }
    for (uint32_t align = (0u - dst) & 15; align != 0; --align) {
        *d++ = *s++;
        --len;
    }
    if (dist == 1) {
        uint8_t fill[16];
        std::memset(fill, *s, 16);
        int32_t left = static_cast<int32_t>(len);
        for (;;) {
            left -= 16;
            std::memcpy(d, fill, 16);
            if (left <= 0) return;
            left -= 16;
            std::memcpy(d + 16, fill, 16);
            d += 32;
            if (left <= 0) return;
        }
    }
    if (dist < 16) return byte_loop(d, s, len);
    for (;;) {
        if (len < 16) return byte_loop(d, s, len);
        uint8_t q[16];
        std::memcpy(q, s, 16);
        std::memcpy(d, q, 16);
        s += 16;
        d += 16;
        len -= 16;
        if (len == 0) return;
    }
}

// ---- the DEFLATE decoder ---------------------------------------------------------------------
constexpr uint32_t kFixedTables = 0x006CEC80;  // 0058AAB8: 0x300 bytes, literal/length then distance
constexpr uint32_t kLengthTables = 0x006CEB60; // halfwords: length bits +0, base +0x40, distance bits +0x80, base +0xC0
constexpr uint32_t kCodeLengthOrder = 0x006CEB40;

// A canonical Huffman table as 0058A7A8 builds it: for code length L (index L-1) the largest code
// (limit, -1 when none) and the first symbol index minus the first code (offset), then the symbols
// sorted by (length, symbol).
struct Table {
    int16_t limit[16];
    int16_t offset[16];
    int16_t sym[320];
};
static_assert(sizeof(Table) == 0x40 + 640);

struct Inflate {
    const uint8_t *in;
    uint64_t bit = 0;  // bits consumed
    uint32_t out, end;  // s3, s4

    uint32_t getbit() {
        const uint32_t b = (in[bit >> 3] >> (bit & 7)) & 1;
        ++bit;
        return b;
    }
    uint32_t getbits(int n) {
        uint32_t v = 0;
        for (int k = 0; k < n; ++k) v |= getbit() << k;
        return v;
    }
    // 0058A5F8: the code is read most significant bit first, one bit at a time; at length 16 the
    // original stops with whatever its tables then give (invalid streams only): -1 here.
    int decode(const Table &t) {
        int32_t code = static_cast<int32_t>(getbit());
        for (int i = 0;; ++i) {
            if (!(t.limit[i] < code)) return t.sym[code + t.offset[i]];
            if (i >= 15) return -1;
            code = (code << 1) | static_cast<int32_t>(getbit());
        }
    }
};

// 0058A7A8: builds a table from n code lengths. Returns 0, or -3 for a length over 15.
int build(Table &t, const uint8_t *lens, int n) {
    int16_t len[320];
    int16_t code[320];
    for (int k = 0; k < 16; ++k) t.limit[k] = -1;
    for (int i = 0; i < n; ++i) {
        t.sym[i] = static_cast<int16_t>(i);
        len[i] = static_cast<int8_t>(lens[i]);
    }
    for (int i = 0; i + 1 < n; ++i)  // exchange sort by (length, symbol), unsigned compares
        for (int j = i + 1; j < n; ++j) {
            const uint32_t li = static_cast<uint32_t>(len[i]), lj = static_cast<uint32_t>(len[j]);
            if (lj < li || (lj == li && static_cast<uint32_t>(t.sym[j]) < static_cast<uint32_t>(t.sym[i]))) {
                std::swap(len[i], len[j]);
                std::swap(t.sym[i], t.sym[j]);
            }
        }
    int cur = 0, c = 0;
    for (int i = 0; i < n; ++i) {
        while (cur != len[i]) {
            if (!(static_cast<uint32_t>(cur) < 16)) return -3;
            ++cur;
            c <<= 1;
        }
        if (cur != 0) code[i] = static_cast<int16_t>(c++);
    }
    int prev = 0, slot = -1;  // slot: the table index of the current length (L - 1)
    for (int i = 0; i < n; ++i) {
        if (prev != len[i]) {
            slot = len[i] - 1;
            prev = len[i];
            t.offset[slot] = static_cast<int16_t>(i - code[i]);
        }
        if (prev != 0) t.limit[slot] = code[i];
    }
    return 0;
}

// 0058A668: n code lengths into out (zeroed by the caller) through the code-length table.
// Returns 0 or -2 (a symbol over 18).
int read_lengths(Inflate &z, const Table &cl, uint8_t *out, uint32_t n) {
    uint32_t i = 0;
    uint8_t *p = out;
    while (i < n) {
        const int s = z.decode(cl);
        if (s >= 0 && s < 16) {
            *p++ = static_cast<uint8_t>(s);
            ++i;
        } else if (s == 16) {
            const uint32_t r = z.getbits(2) + 3;
            const uint8_t prev = p > out ? p[-1] : 0;
            for (uint32_t k = 0; k < r; ++k) p[k] = prev;
            p += r;
            i += r;
        } else if (s == 17) {
            const uint32_t r = z.getbits(3) + 3;
            p += r;
            i += r;
        } else if (s == 18) {
            const uint32_t r = z.getbits(7) + 11;
            p += r;
            i += r;
        } else {
            return -2;
        }
    }
    return 0;
}

// 0058AB08: decodes one Huffman block. Returns 0 at the end of the block, or -4 (bad symbol),
// -6 (output full).
int codes(Inflate &z, const Table &lit, const Table &dist) {
    const auto *lt = static_cast<const int16_t *>(guest_ptr(kLengthTables));
    for (;;) {
        const int s = z.decode(lit);
        if (s >= 0 && s < 256) {
            if (z.end - z.out == 0) return -6;
            *static_cast<uint8_t *>(guest_ptr(z.out)) = static_cast<uint8_t>(s);
            ++z.out;
            continue;
        }
        const int l = s - 0x101;
        if (l < 0 && s == 256) return 0;
        if (l < 0 || l >= 29) return -4;
        uint32_t len = static_cast<uint32_t>(lt[0x20 + l]);
        if (const int nb = lt[l]) len += z.getbits(nb);
        const int ds = z.decode(dist);
        if (ds < 0 || ds >= 30) return -4;
        uint32_t d = static_cast<uint32_t>(lt[0x60 + ds]);
        if (const int nb = lt[0x40 + ds]) d += z.getbits(nb);
        if (z.end - z.out < len) return -6;
        const uint32_t o = z.out;
        z.out += len;
        packed_memcpy(o, o - d, len);
    }
}

// ---- the soft-float double routines (libgcc's dpadd/dpmul/dpcmp, written in assembly) -----------
// Ported instruction by instruction on the IEEE bit patterns: they truncate (no rounding), treat
// denormals as zero and return their own NaNs; native code does its double arithmetic itself, so
// these only serve calls through the original addresses.
inline uint32_t lzc_minus1(uint32_t w) {  // plzcw on one word: leading bits equal to bit 31, minus 1
    const uint32_t u = (w & 0x80000000u) ? ~w : w;
    int n = 0;
    while (n < 31 && !(u & (0x40000000u >> n))) ++n;
    return static_cast<uint32_t>(n);
}

uint64_t dpadd(uint64_t a0, uint64_t a1) {
    const uint64_t v0s = a0 << 1, v1s = a1 << 1;
    const uint64_t at = v1s < v0s;
    const uint64_t t7m = ~uint64_t{0} >> 12;
    if (at == 0) std::swap(a0, a1);
    const uint64_t a2 = (a0 >> 52) & 0x7FF, a3 = (a1 >> 52) & 0x7FF;
    const uint64_t t8i = t7m + 1;
    const uint64_t t5d = a2 - a3;
    uint64_t t1 = a1 & t7m, t0 = a0 & t7m;
    if (a2 == 0x7FF) {  // 0057F6A4: the larger operand is an infinity or a NaN
        const uint64_t qnan = uint64_t{0x7FF8} << 48;
        const uint64_t t5 = t0 | t1;
        uint64_t t2 = a0 | qnan, t3 = a1 | qnan;
        if (t0 == 0) t2 = a0;
        if (a3 != 0x7FF) return t2;
        if (t1 == 0) t3 = a1;
        if (t5 == 0) return (a0 ^ a1) ? qnan : t2;  // inf - inf
        if (t1 == 0) return t2;
        return at == 0 ? t3 : t2;
    }
    t1 |= t8i;
    t0 |= t8i;
    if (a3 == 0) t1 = 0;
    if (a2 == 0) t0 = 0;
    t1 <<= 8;
    if (t5d >> 6) t1 = 0;
    t0 <<= 8;
    const uint64_t t7 = t1 >> (t5d & 63);
    const uint64_t sd = static_cast<uint64_t>(static_cast<int64_t>(a0 ^ a1) >> 63);
    const uint64_t lost = (t7 << (t5d & 63)) ^ t1;
    const uint64_t t6 = lost == 0 ? t7 : (t7 | 1);  // sticky bit
    uint64_t t4 = t0 - sd + (t6 ^ sd);
    const uint64_t sign = (a0 >> 63) << 63;
    const uint64_t t9 = t4 >> 61;
    const uint64_t t8 = t4 & 1;
    t4 >>= t9;
    int64_t t3 = static_cast<int64_t>(a2 + t9);
    const uint32_t c0 = lzc_minus1(static_cast<uint32_t>(t4)), c1 = lzc_minus1(static_cast<uint32_t>(t4 >> 32));
    int64_t v0 = (c0 & 0x1F) + 1;
    const int64_t v1 = static_cast<int64_t>(c1) - 2;
    if (t4 >> 31) v0 = 0;
    const int64_t shift = v1 + v0;
    t4 |= t8;
    t3 -= shift;
    t4 <<= (shift & 63);
    if ((t0 | t1) == 0) return static_cast<uint64_t>(static_cast<int64_t>(a0 & a1) >> 63) << 63;
    // pmaxw t6, (1 - t3), 0: per 32-bit word; t3 fits in a word here
    int32_t den = static_cast<int32_t>(1 - t3);
    if (den < 0) den = 0;
    t4 >>= (static_cast<uint32_t>(den) & 63);
    if (den != 0) t3 = 0;
    if (t3 == 0) t4 = 0;
    if (!(t3 < 0x7FF)) {
        t4 = 0;
        t3 = 0x7FF;
    }
    t4 <<= 4;
    return sign | (static_cast<uint64_t>(t3) << 52) | (t4 >> 12);
}

uint64_t dpmul(uint64_t a0, uint64_t a1) {
    const uint64_t t3m = ~uint64_t{0} >> 12, t4i = t3m + 1;
    uint64_t v0 = ((a0 ^ a1) >> 63) << 63;
    const uint64_t a2 = (a0 >> 52) & 0x7FF, a3 = (a1 >> 52) & 0x7FF;
    uint64_t t0 = (a0 & t3m) | t4i, t1 = (a1 & t3m) | t4i;
    if (a2 == 0) t0 = 0;
    if (a3 == 0) t1 = 0;
    auto nan_or_inf = [&](uint64_t t) {  // 0057F9D8: an operand is infinite or NaN, the other zero
        const uint64_t q = uint64_t{0x7FF8} << 48;
        t >>= 1;
        const uint64_t m = t << 12;
        t |= q;
        if (m == 0) {
            t = q;
            v0 = 0;
        }
        return v0 | t;
    };
    auto special = [&](uint64_t t) {  // 0057FA14
        t >>= 1;
        const uint64_t m = t << 12;
        t |= uint64_t{0x7FF} << 51;
        if (m == 0) t = uint64_t{0x7FF} << 52;
        return v0 | t;
    };
    if (t0 == 0) return a3 == 0x7FF ? nan_or_inf(a1 << 1) : v0;
    if (t1 == 0) return a2 == 0x7FF ? nan_or_inf(a0 << 1) : v0;
    if (a2 == 0x7FF) {
        if (a3 != 0x7FF || (a0 << 12) != 0) return special(a0 << 1);
        return special(a1 << 1);
    }
    if (a3 == 0x7FF) return special(a1 << 1);
    const uint64_t t0lo = t0 & 0xFFFFFFFFu, t0hi = t0 >> 32, t1lo = t1 & 0xFFFFFFFFu, t1hi = t1 >> 32;
    uint64_t t8 = t0lo * t1lo, t9 = t0hi * t1hi;
    const uint64_t t6 = t0hi * t1lo, t7c = t0lo * t1hi;
    const uint64_t cross = t6 + t7c;
    int64_t e = static_cast<int64_t>(a2 + a3);
    const uint64_t lo_add = cross << 32;
    t8 += lo_add;
    t9 += cross >> 32;
    const uint64_t c_cross = cross < t7c, c_lo = t8 < lo_add;
    t9 += c_lo;
    e -= 0x3FF;
    t9 += c_cross << 32;
    const uint64_t top = t9 >> 41;
    e += static_cast<int64_t>(top);
    const uint64_t sh = 20 - top;
    uint64_t m = (t9 << (sh & 63)) | (t8 >> ((0 - sh) & 63));
    if (e < 1) {
        e = 0;
        m = 0;
    }
    if (!(e < 0x7FF)) {
        m = 0;
        e = 0x7FF;
    }
    return v0 | (static_cast<uint64_t>(e) << 52) | ((m >> 8) & t3m);
}

int dpcmp(uint64_t a0, uint64_t a1) {
    const uint64_t inf2 = uint64_t{0x7FF} << 53;
    uint64_t a2 = a0 << 1, a3 = a1 << 1;
    if ((a2 >> 53) == 0) a2 = 0;
    if ((a3 >> 53) == 0) a3 = 0;
    const int64_t t4 = static_cast<int64_t>(a0) >> 63, t5 = static_cast<int64_t>(a1) >> 63;
    const int64_t x = static_cast<int64_t>(a0 ^ (static_cast<uint64_t>(t4) >> 1));
    const int64_t y = static_cast<int64_t>(a1 ^ (static_cast<uint64_t>(t5) >> 1));
    const int nan = (inf2 < a2) | (inf2 < a3);
    int64_t t9 = t4 - t5;
    if (((a2 ^ inf2) | (a2 ^ a3)) != 0) t9 = 0;
    int64_t v0 = static_cast<int64_t>(y < x) - static_cast<int64_t>(x < y);
    if ((a2 ^ a3) == 0) v0 = t9;
    if (nan) v0 = nan;
    return static_cast<int>(v0);
}

}  // namespace

extern "C" {

// 0057F578 dpadd, 0057F8B8 dpmul (384 bytes each), 0057F6F8 dpcmp (112 bytes): the routines above,
// on doubles passed as their bit patterns (the EE passes them in 64-bit registers).
uint64_t gt4_replace_func_0057F578(uint64_t a, uint64_t b) { return dpadd(a, b); }
uint64_t gt4_replace_func_0057F8B8(uint64_t a, uint64_t b) { return dpmul(a, b); }
int gt4_replace_func_0057F6F8(uint64_t a, uint64_t b) { return dpcmp(a, b); }

// 0058ACB0 (352 bytes): the packed memcpy above.
void gt4_replace_func_0058ACB0(uint32_t dst, uint32_t src, uint32_t len) { packed_memcpy(dst, src, len); }

// 0058A4A8 (with 0058A280-0058AC20): inflates the raw DEFLATE stream at `in` into out[0, capacity).
// Returns the bytes written and, when `end` is not null, stores there the first input byte after
// the stream; or an error: -1 block type 3, -2 bad code-length symbol, -3 bad code length, -4 bad
// literal/length or distance symbol, -5 stored block length check, -6 output buffer full.
int gt4_replace_func_0058A4A8(uint32_t out, int capacity, const uint8_t *in, uint32_t *end) {
    Inflate z{in, 0, out, out + static_cast<uint32_t>(capacity)};
    static thread_local Table lit, dist, cl;
    int last;
    do {
        last = static_cast<int>(z.getbit());
        const uint32_t type = z.getbits(2);
        int r = 0;
        if (type == 0) {  // 0058AC20: stored
            z.bit = (z.bit + 7) & ~uint64_t{7};
            const uint8_t *p = in + (z.bit >> 3);
            const uint32_t w = static_cast<uint32_t>(p[0]) | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24;
            const uint32_t n = w & 0xFFFF;
            if ((~w >> 16) != n) return -5;
            if (z.end - z.out < n) return -6;
            const uint32_t src = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p + 4));
            const uint32_t o = z.out;
            z.out += n;
            packed_memcpy(o, src, n);
            z.bit += 8 * (4 + static_cast<uint64_t>(n));
        } else if (type == 1) {  // 0058AAB8: the fixed tables, prebuilt in the image
            const auto *f = static_cast<const uint8_t *>(guest_ptr(kFixedTables));
            std::memcpy(&lit, f, 0x280);
            std::memcpy(&dist, f + 0x280, 0x80);
            r = codes(z, lit, dist);
        } else if (type == 2) {  // 0058A950: dynamic tables
            const uint32_t hlit = z.getbits(5) + 257, hdist = z.getbits(5) + 1, hclen = z.getbits(4) + 4;
            uint8_t lens[1024];
            std::memset(lens, 0, 32);
            const auto *order = static_cast<const uint8_t *>(guest_ptr(kCodeLengthOrder));
            for (uint32_t i = 0; i < hclen; ++i) lens[order[i]] = static_cast<uint8_t>(z.getbits(3));
            if ((r = build(cl, lens, 19)) != 0) return r;
            std::memset(lens, 0, sizeof lens);
            if ((r = read_lengths(z, cl, lens, hlit)) != 0) return r;
            if ((r = build(lit, lens, static_cast<int>(hlit))) != 0) return r;
            std::memset(lens, 0, sizeof lens);
            if ((r = read_lengths(z, cl, lens, hdist)) != 0) return r;
            if ((r = build(dist, lens, static_cast<int>(hdist))) != 0) return r;
            r = codes(z, lit, dist);
        } else {
            return -1;
        }
        if (r != 0) return r;
    } while (!last);
    if (end) *end = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(in + ((z.bit + 7) >> 3)));
    return static_cast<int>(z.out - out);
}

// 0058B320 (8 bytes; with seven more stubs to 0058B360): liberx's import stub for modload export
// 0xC, _sceErxLoadStartModuleFile (005870C0). The stubs (`break 0,1; addiu $zero, N`, tables
// "ErXsTuB" at 0068B9A0: sysmem 0058B120, loadcore 0058B198, modload 0058B2E8) trap until
// sceErxInit links them (_sceErxRegisterLibraryEntries rewrites each to `j export`); the only
// callers, sceErxInit (00583718) and 00576E10, run after that. Natively the stub goes where the
// link would make it go, through the export's trampoline.
int gt4_replace_func_0058B320(const char *path, int a, int b, int c, int d, int *result) {
    using Fn = int (*)(const char *, int, int, int, int, int *);
    return reinterpret_cast<Fn>(guest_ptr(0x005870C0))(path, a, b, c, d, result);
}

// 0058A280 (the 64-bit bit-buffer refills, each jumping back into its reader), 0058A5F8 (one
// Huffman code), 0058A668 (code lengths), 0058A7A8 (table builder), 0058A950 (dynamic block
// header), 0058AB08 (Huffman block), 0058AC20 (stored block): the inflater's inner pieces. They
// keep the stream state in $s5-$s7 as 0058A4A8 sets it up and are reached only from inside the
// routine (no call from outside 0058A280-0058ACB0, no pointer to them in the image); 0058A4A8 is
// replaced as a whole above, so natively nothing reaches them.
[[noreturn]] static void inflate_piece(uint32_t addr) {
    gt4::rt::fatal("%08X: an inner piece of the DEFLATE decoder (entry 0058A4A8) called directly", addr);
}
void gt4_replace_func_0058A280(void) { inflate_piece(0x0058A280); }
void gt4_replace_func_0058A5F8(void) { inflate_piece(0x0058A5F8); }
void gt4_replace_func_0058A668(void) { inflate_piece(0x0058A668); }
void gt4_replace_func_0058A7A8(void) { inflate_piece(0x0058A7A8); }
void gt4_replace_func_0058A950(void) { inflate_piece(0x0058A950); }
void gt4_replace_func_0058AB08(void) { inflate_piece(0x0058AB08); }
void gt4_replace_func_0058AC20(void) { inflate_piece(0x0058AC20); }

}  // extern "C"
