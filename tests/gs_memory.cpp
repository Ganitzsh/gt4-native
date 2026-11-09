// The GS memory model's fast paths against its pixel-by-pixel reference (readPixel / writePixel): the palette
// loads (readClut) and the uploads (write), on random memory, for every format, at block addresses up to the top
// of memory (the addresses wrap).
#include "gs_memory.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <random>
#include <vector>

using gt4::gsn::GsMemory;
namespace psm = gt4::gsn;

namespace {

int failures = 0;

void expect(bool ok, const char* what) {
    if (ok) return;
    std::fprintf(stderr, "FAILED: %s\n", what);
    ++failures;
}

// readClut as it was: each entry through readPixel, in CSM1's order.
void referenceClut(const GsMemory& m, const GsMemory::Tex0& t, uint32_t* entries) {
    const bool four = t.psm == psm::PSMT4 || t.psm == psm::PSMT4HL || t.psm == psm::PSMT4HH;
    const uint32_t count = four ? 16 : 256;
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
        entries[i] = m.readPixel(t.cpsm == psm::PSMCT24 ? psm::PSMCT32 : t.cpsm, t.cbp, 1, x, y);
    }
}

void testReadClut(GsMemory& m, std::mt19937& rng) {
    const uint32_t cpsms[] = {psm::PSMCT32, psm::PSMCT24, psm::PSMCT16, psm::PSMCT16S};
    const uint32_t psms[] = {psm::PSMT8, psm::PSMT4, psm::PSMT8H, psm::PSMT4HL, psm::PSMT4HH};
    bool same = true;
    for (int round = 0; round < 400 && same; ++round) {
        GsMemory::Tex0 t;
        t.cpsm = cpsms[round % 4];
        t.psm = psms[(round / 4) % 5];
        t.cbp = round < 8 ? 0x3FFF - round : std::uniform_int_distribution<uint32_t>(0, 0x3FFF)(rng);   // the top: wraps
        uint32_t want[256] = {}, got[256] = {};
        referenceClut(m, t, want);
        m.readClut(t, got);
        same = std::memcmp(want, got, sizeof want) == 0;
        if (!same) std::fprintf(stderr, "  readClut: cpsm %02X psm %02X cbp %04X differs\n", t.cpsm, t.psm, t.cbp);
    }
    expect(same, "readClut matches readPixel for CT32/CT24/CT16/CT16S palettes, 16 and 256 entries, any CBP");
}

// Uploads (beginWrite / write) against writePixel pixel by pixel: every format, rectangles of odd sizes at odd
// positions (blocks covered in part), block addresses up to the top of memory, the bytes sent in chunks of random
// sizes (pixels split across calls); the memory and each call's "changed" must match.
void testWrites(std::mt19937& rng) {
    const uint32_t formats[] = {psm::PSMCT32, psm::PSMCT24, psm::PSMCT16, psm::PSMCT16S, psm::PSMT8, psm::PSMT4, psm::PSMT8H,
                                psm::PSMT4HL, psm::PSMT4HH, psm::PSMZ32, psm::PSMZ24, psm::PSMZ16, psm::PSMZ16S};
    auto random = [&](uint32_t lo, uint32_t hi) { return std::uniform_int_distribution<uint32_t>(lo, hi)(rng); };
    auto fast = std::make_unique<GsMemory>(), slow = std::make_unique<GsMemory>();
    bool same = true;
    for (const uint32_t f : formats) {
        for (int round = 0; round < 40 && same; ++round) {
            for (size_t i = 0; i < GsMemory::kBytes; i += 4) {   // (the same random contents in both)
                const uint32_t v = rng();
                std::memcpy(fast->data() + i, &v, 4);
            }
            std::memcpy(slow->data(), fast->data(), GsMemory::kBytes);
            GsMemory::Transfer t;
            t.dpsm = f;
            t.dbp = round < 4 ? 0x3FFF - random(0, 40) : random(0, 0x3FFF);
            t.dbw = (f == psm::PSMT8 || f == psm::PSMT4) ? 2 * random(1, 5) : random(1, 10);
            t.rrw = random(1, 70);
            t.rrh = random(1, 24);
            t.dsax = random(0, t.dbw * 64 > t.rrw ? t.dbw * 64 - t.rrw : 0);
            t.dsay = random(0, 2047 - t.rrh);
            if (round % 3 == 0) t.dsax &= ~7u, t.rrw = std::max<uint32_t>(8, t.rrw & ~7u);   // block-aligned rows too
            const uint32_t bits = gt4::gsn::psmBits(f);
            const size_t pixels = size_t(t.rrw) * t.rrh, bytes = bits == 4 ? (pixels + 1) / 2 : pixels * (bits / 8);
            std::vector<uint8_t> data(bytes);
            for (auto& b : data) b = static_cast<uint8_t>(rng());
            // the pixels' values as the reference takes them
            std::vector<uint32_t> values(pixels);
            for (size_t p = 0; p < pixels; ++p) {
                if (bits == 4) values[p] = (data[p / 2] >> ((p & 1) * 4)) & 15;
                else {
                    uint32_t v = 0;
                    std::memcpy(&v, &data[p * (bits / 8)], bits / 8);
                    values[p] = v;
                }
            }
            const uint32_t mask = bits >= 32 ? ~0u : (1u << bits) - 1;
            fast->beginWrite(t);
            size_t sent = 0, done = 0;   // bytes sent, pixels the reference has written
            while (sent < bytes) {
                const size_t chunk = std::min<size_t>(bytes - sent, round % 2 ? random(1, 97) : random(1, 4096));
                const bool changed = fast->write(data.data() + sent, chunk);
                sent += chunk;
                // the pixels complete after these bytes
                const size_t complete = bits == 4 ? std::min(pixels, sent * 2) : sent / (bits / 8);
                bool want = false;
                for (; done < complete; ++done) {
                    const uint32_t x = t.dsax + uint32_t(done % t.rrw), y = t.dsay + uint32_t(done / t.rrw);
                    if (slow->readPixel(f, t.dbp, t.dbw, x, y) != (values[done] & mask)) want = true;
                    slow->writePixel(f, t.dbp, t.dbw, x, y, values[done]);
                }
                if (changed != want) {
                    same = false;
                    std::fprintf(stderr, "  write: psm %02X, a call's \"changed\" is %d, the pixels say %d\n", f, changed, want);
                    break;
                }
            }
            if (same && std::memcmp(fast->data(), slow->data(), GsMemory::kBytes) != 0) {
                same = false;
                std::fprintf(stderr, "  write: psm %02X bp %04X bw %u at %u,%u %ux%u: memory differs\n", f, t.dbp, t.dbw, t.dsax, t.dsay, t.rrw, t.rrh);
            }
        }
    }
    expect(same, "uploads match writePixel for every format, odd rectangles and positions, split pixels");
}

// decode's 32/24-bit block path (whole 8x8 blocks) against readPixel with decode's own alpha rule, pixel by pixel:
// CT32, CT24, Z32, Z24, random TBP (up to the top of memory: the addresses wrap), TBW, alpha settings, block-aligned
// rectangles anywhere up to the 2047 wrap, and unaligned ones (the general path) for good measure.
void testDecode(const GsMemory& m, std::mt19937& rng) {
    const uint32_t psms[] = {psm::PSMCT32, psm::PSMCT24, psm::PSMZ32, psm::PSMZ24};
    auto pick = [&](uint32_t lo, uint32_t hi) { return std::uniform_int_distribution<uint32_t>(lo, hi)(rng); };
    bool same = true;
    for (int round = 0; round < 600 && same; ++round) {
        GsMemory::Tex0 t;
        t.psm = psms[round % 4];
        t.tbp = round < 8 ? 0x3FFF - round * 32 : pick(0, 0x3FFF);
        t.tbw = pick(1, 16);
        psm::Texa texa;
        texa.ta0 = static_cast<uint8_t>(pick(0, 255));
        texa.aem = pick(0, 1) != 0;
        const bool aligned = round % 5 != 4;
        const uint32_t u0 = aligned ? pick(0, 255) * 8 : pick(0, 2047);
        const uint32_t v0 = aligned ? pick(0, 255) * 8 : pick(0, 2047);
        const uint32_t w = aligned ? pick(1, 12) * 8 : pick(1, 70), h = aligned ? pick(1, 12) * 8 : pick(1, 70);
        std::vector<uint32_t> got(size_t(w) * h), want(size_t(w) * h);
        m.decode(t, texa, u0, v0, w, h, got.data());
        const bool rgb = t.psm == psm::PSMCT24 || t.psm == psm::PSMZ24;
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < w; ++x) {
                uint32_t raw = m.readPixel(t.psm, t.tbp, t.tbw, u0 + x, v0 + y);
                if (rgb) raw = (raw & 0xFFFFFF) | (uint32_t(texa.aem && !(raw & 0xFFFFFF) ? 0 : texa.ta0) << 24);
                want[size_t(y) * w + x] = raw;
            }
        same = got == want;
        if (!same)
            std::fprintf(stderr, "  decode: psm %02X tbp %04X tbw %u at %u,%u %ux%u differs\n", t.psm,
                         t.tbp, t.tbw, u0, v0, w, h);
    }
    expect(same, "decode's block path matches readPixel for CT32/CT24/Z32/Z24, any TBP and TBW, up to the wraps");
}

}  // namespace

int main() {
    auto m = std::make_unique<GsMemory>();
    std::mt19937 rng(11);
    for (size_t i = 0; i < GsMemory::kBytes; i += 4) {
        const uint32_t v = rng();
        std::memcpy(m->data() + i, &v, 4);
    }
    testReadClut(*m, rng);
    testDecode(*m, rng);
    testWrites(rng);
    std::printf("gs_memory: %s\n", failures ? "FAILED" : "fast paths match the reference");
    return failures ? 1 : 0;
}
