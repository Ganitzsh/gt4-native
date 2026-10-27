// The native VU1 microprograms (platform/gs/vu1_native.cpp) against the reference interpreter
// (vu1_ref.cpp), on random batches: every entry of GT4's table, every lighting chain ITOP can select,
// both VIF buffers, and the two screen-grid programs. The PATH1 packets and the data memory below the
// clipper's scratch area (qword 870 x, y included) must be identical.
//
// Needs GT4's micro image, which is game data: GT4_VU1_IMAGE=path (the images/*.bin a GT4_VU1_INVENTORY
// run writes). Without it the test is skipped (exit code 77). Optional: GT4_VU1_FUZZ_ROUNDS (default 60).
#include "vu1.h"
#include "vu1_native.h"
#include "vu1_ref.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <vector>

using gt4::ps2::GifSink;
using gt4::ps2::Vu1;
using gt4::ps2::Vu1Native;
using gt4::ps2::Vu1Ref;

namespace {

struct Collect final : GifSink {
    std::vector<uint8_t> bytes;
    void gif(uint32_t, const uint8_t* data, size_t size) override { bytes.insert(bytes.end(), data, data + size); }
};

struct Rng {
    std::mt19937 engine;
    explicit Rng(uint32_t seed) : engine(seed) {}
    float real(float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(engine); }
    int integer(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(engine); }
    bool chance(float p) { return real(0.0f, 1.0f) < p; }
};

void putFloat(Vu1& vu, uint32_t q, int lane, float f) { std::memcpy(vu.data + (q & 1023) * 16 + lane * 4, &f, 4); }
void putWord(Vu1& vu, uint32_t q, int lane, uint32_t w) { std::memcpy(vu.data + (q & 1023) * 16 + lane * 4, &w, 4); }

// Which representation the lighting chain's first routine expects (byte address of the routine).
enum class Layout { Floats, Integers, IntegerNormalsInColour, IntegerNormalsInSt };

// A batch at `top` the renderers can walk: 1..4 strips of 3+ vertices (32 at most, so that the key-frame
// blend's second frame stays inside the batch), a perspective-like transform with some vertices behind
// the camera or outside the guard band, and constants in the ranges GT4 uses.
void randomBatch(Vu1& vu, uint32_t top, Rng& r, Layout layout, bool lit) {
    for (uint32_t q = 0; q < 870; ++q)
        for (int lane = 0; lane < 4; ++lane) putWord(vu, q, lane, static_cast<uint32_t>(r.engine()) & 0x3FFFFFFFu);
    // Strips.
    const int strips = r.integer(1, 4);
    int vertices = 0, lengths[4];
    for (int k = 0; k < strips; ++k) {
        lengths[k] = r.integer(3, 8);   // a strip has a triangle: with 2 vertices the loops never end (length 0)
        vertices += lengths[k];
    }
    const float spread = r.chance(0.3f) ? 400.0f : 30.0f;   // large: many triangles cross the guard band
    for (int v = 0; v < 64; ++v) {
        const float x = r.real(-spread, spread), y = r.real(-spread, spread), z = r.real(-spread, spread);
        if (layout == Layout::Floats) {
            putFloat(vu, top + v, 0, x);
            putFloat(vu, top + v, 1, y);
            putFloat(vu, top + v, 2, z);
        } else {
            putWord(vu, top + v, 0, static_cast<uint32_t>(static_cast<int>(x * 10)));
            putWord(vu, top + v, 1, static_cast<uint32_t>(static_cast<int>(y * 10)));
            putWord(vu, top + v, 2, static_cast<uint32_t>(static_cast<int>(z * 10)));
        }
        putFloat(vu, top + v, 3, 1.0f);
        // ST (or normals for untextured models).
        if (layout == Layout::Integers || layout == Layout::IntegerNormalsInColour) {
            putWord(vu, top + 64 + v, 0, static_cast<uint32_t>(r.integer(0, 4096)));
            putWord(vu, top + 64 + v, 1, static_cast<uint32_t>(r.integer(0, 4096)));
        } else if (layout == Layout::IntegerNormalsInSt) {
            for (int i = 0; i < 3; ++i) putWord(vu, top + 64 + v, i, static_cast<uint32_t>(r.integer(-127, 127)));
        } else {
            const float nx = r.real(-1, 1), ny = r.real(-1, 1), nz = r.real(-1, 1);
            const float len = std::sqrt(nx * nx + ny * ny + nz * nz) + 1e-3f;
            putFloat(vu, top + 64 + v, 0, lit ? nx / len : r.real(0, 1));
            putFloat(vu, top + 64 + v, 1, lit ? ny / len : r.real(0, 1));
            putFloat(vu, top + 64 + v, 2, lit ? nz / len : r.real(0, 1));
        }
        // Colours, or normals for lit models.
        if (layout == Layout::IntegerNormalsInColour) {
            for (int i = 0; i < 3; ++i) putWord(vu, top + 128 + v, i, static_cast<uint32_t>(r.integer(-127, 127)));
        } else if (lit && layout == Layout::Floats && r.chance(0.5f)) {
            const float nx = r.real(-1, 1), ny = r.real(-1, 1), nz = r.real(-1, 1);
            const float len = std::sqrt(nx * nx + ny * ny + nz * nz) + 1e-3f;
            putFloat(vu, top + 128 + v, 0, nx / len);
            putFloat(vu, top + 128 + v, 1, ny / len);
            putFloat(vu, top + 128 + v, 2, nz / len);
        } else {
            for (int i = 0; i < 4; ++i) putWord(vu, top + 128 + v, i, static_cast<uint32_t>(r.integer(0, 255)));
        }
    }
    putWord(vu, top + 64, 3, static_cast<uint32_t>(strips));
    for (int k = 0; k < strips; ++k) putWord(vu, top + 65 + k, 3, static_cast<uint32_t>(3 * (lengths[k] - 2)));
    // The packet's tag: NLOOP vertices, EOP, PRE and a triangle strip, ST/RGBAQ/XYZF2.
    putWord(vu, top + 192, 0, 0x8000u | static_cast<uint32_t>(vertices));
    putWord(vu, top + 192, 1, 0x30000000u | (0x3Cu << 15) | (1u << 14));
    putWord(vu, top + 192, 2, 0x412u);
    putWord(vu, top + 192, 3, 0);
    // Transform: a random rotation-ish matrix, w = distance along a view axis plus an offset.
    const float depth = r.real(-20.0f, 60.0f);
    for (int c = 0; c < 4; ++c)
        for (int i = 0; i < 4; ++i) putFloat(vu, top + 193 + c, i, c == 3 ? (i == 3 ? depth : r.real(-5, 5)) : r.real(-1, 1));
    const float scale[4] = {r.real(200, 400), r.real(-250, -150), r.real(-1e6f, -1e5f), r.real(-200, -50)};
    const float offset[4] = {2048.0f, 2048.0f, r.real(1e6f, 1.6e7f), r.real(100, 255)};
    const float clampv[4] = {r.real(1e3f, 1e5f), r.real(1e3f, 1e5f), r.real(1e3f, 1e5f), r.real(5.0f, 200.0f)};
    const float guard[4] = {r.real(0.3f, 0.6f), r.real(0.3f, 0.6f), 1.0f, 1.0f};
    for (int i = 0; i < 4; ++i) {
        putFloat(vu, top + 197, i, scale[i]);
        putFloat(vu, top + 198, i, offset[i]);
        putFloat(vu, top + 199, i, clampv[i]);
        putFloat(vu, top + 200, i, guard[i]);
    }
    // Lighting constants: light matrices, ambient/eye, coefficients, clamps.
    for (uint32_t q = 201; q <= 216; ++q)
        for (int i = 0; i < 4; ++i) putFloat(vu, top + q, i, r.real(-1.0f, 1.0f));
    for (int i = 0; i < 4; ++i) {
        putFloat(vu, top + 204, i, r.real(-40.0f, 40.0f));   // ambient, or the eye position
        putFloat(vu, top + 209, i, 255.0f);
        putFloat(vu, top + 216, i, 255.0f);
    }
    // The fade body's table: qword 202 .x an offset, the entry's .x the alpha (0: nothing drawn).
    const uint32_t fade = static_cast<uint32_t>(r.integer(14, 20));
    putWord(vu, top + 202, 0, fade);
    putWord(vu, top + 203 + fade, 0, r.chance(0.2f) ? 0u : static_cast<uint32_t>(r.integer(1, 255)));
    // A state packet at 217 (A+D, two registers) and the key-frame weight in 242 .w.
    putWord(vu, top + 217, 0, 0x8002u);
    putWord(vu, top + 217, 1, 0x10000000u);
    putWord(vu, top + 217, 2, 0x0Eu);
    putWord(vu, top + 217, 3, 0);
    putFloat(vu, top + 242, 3, r.real(0.0f, 1.0f));
    // The registers an entry without setup reads come from an earlier setup.
    static const int registers[8] = {28, 29, 30, 31, 25, 26, 24, 23};
    for (int k = 0; k < 8; ++k) std::memcpy(&vu.vf[registers[k]], vu.data + ((top + 193 + k) & 1023) * 16, 16);
    putWord(vu, 870, 1, r.chance(0.5f) ? 25u : 0u);
}

// Grid programs: a template packet at TOP + 6, offsets inside it, small counts.
void randomGrid(Vu1& vu, uint32_t top, Rng& r, bool ramps) {
    for (uint32_t q = 0; q < 870; ++q)
        for (int lane = 0; lane < 4; ++lane) putWord(vu, q, lane, static_cast<uint32_t>(r.engine()) & 0x3FFFFFFFu);
    const uint32_t pairs = static_cast<uint32_t>(r.integer(14, 40));
    putWord(vu, top, 1, 6);
    putWord(vu, top, 2, pairs);
    putWord(vu, top, 3, r.chance(0.5f) ? 1u : 0u);
    putWord(vu, top + 6, 0, 0x8000u | static_cast<uint32_t>(r.integer(1, 20)));   // the template's tag
    putWord(vu, top + 6, 1, 0x10000000u);
    for (int k = 0; k < 4; ++k) putWord(vu, top + 1, k, static_cast<uint32_t>(r.integer(1, 3)));
    if (ramps) {
        putWord(vu, top + 1, 1, static_cast<uint32_t>(r.integer(1, 3)));
        putWord(vu, top + 3, 2, static_cast<uint32_t>(r.integer(0, 9)));
        putWord(vu, top + 3, 3, static_cast<uint32_t>(r.integer(0, 6)));
        putWord(vu, top + 2, 0, 0x9D309180u);
        putWord(vu, top + 2, 1, 0x94000000u);
        putWord(vu, top + 2, 2, 0x91000000u);
        putFloat(vu, top + 4, 2, 1.0f);
        putFloat(vu, top + 4, 3, 1.5258789e-5f);
        putFloat(vu, top + 5, 2, 16.0005f);
        putFloat(vu, top + 5, 3, 2.4414062e-4f);
    } else {
        putWord(vu, top + 2, 2, static_cast<uint32_t>(r.integer(0, 12)));
        for (int k = 0; k < 4; ++k) {
            putWord(vu, top + 2 + k, 0, 0x9D300000u + static_cast<uint32_t>(r.integer(0, 0xFFFF)));
            putWord(vu, top + 2 + k, 1, 0x94000000u);
        }
    }
}

}  // namespace

int main() {
    const char* imagePath = std::getenv("GT4_VU1_IMAGE");
    if (!imagePath) {
        std::printf("vu1_native_fuzz: GT4_VU1_IMAGE is not set (GT4's micro image is game data); skipped\n");
        return 77;
    }
    std::FILE* f = std::fopen(imagePath, "rb");
    if (!f) {
        std::fprintf(stderr, "vu1_native_fuzz: cannot open %s\n", imagePath);
        return 1;
    }
    auto vu = std::make_unique<Vu1>();
    const size_t read = std::fread(vu->micro, 1, sizeof vu->micro, f);
    std::fclose(f);
    if (read != sizeof vu->micro) {
        std::fprintf(stderr, "vu1_native_fuzz: %s is not a 16 KiB micro image\n", imagePath);
        return 1;
    }
    const int rounds = std::getenv("GT4_VU1_FUZZ_ROUNDS") ? std::atoi(std::getenv("GT4_VU1_FUZZ_ROUNDS")) : 60;
    auto native = std::make_unique<Vu1>();
    auto ref = std::make_unique<Vu1Ref>();
    Vu1Native programs;
    Rng r(12345);
    uint64_t runs = 0, notNative = 0, failures = 0;
    auto check = [&](uint32_t pc, uint32_t top, uint32_t itop, const char* what) {
        vu->top = top;
        vu->itop = itop;
        *native = *vu;
        Collect nativeKicks;
        native->setGifSink(&nativeKicks);
        ++runs;
        if (!programs.run(*native, pc)) {
            ++notNative;
            return;
        }
        ref->load(*vu);
        ref->sink = nullptr;
        ref->run(pc);
        const bool kicks = nativeKicks.bytes != ref->kicks;
        const bool data = std::memcmp(native->data, ref->data, 870 * 16 + 8) != 0;
        if ((kicks || data) && failures == 0 && std::getenv("GT4_VU1_FUZZ_TRACE")) {   // the first failure, traced
            ref->load(*vu);
            ref->trace = std::fopen("fuzz_ref_trace.txt", "w");
            ref->run(pc);
            std::fclose(ref->trace);
            ref->trace = nullptr;
        }
        if ((kicks || data) && ++failures <= 12) {
            std::fprintf(stderr, "FAILED: %s, entry %04X, TOP %u, ITOP %04X: %s%s (PATH1 %zu/%zu bytes)\n", what, pc * 8, top, itop * 8,
                         kicks ? "PATH1 " : "", data ? "data" : "", nativeKicks.bytes.size(), ref->kicks.size());
            int shown = 0;
            for (size_t q = 0; q * 16 < std::min(nativeKicks.bytes.size(), ref->kicks.size()) && shown < 4; ++q) {
                if (!std::memcmp(nativeKicks.bytes.data() + q * 16, ref->kicks.data() + q * 16, 16)) continue;
                uint32_t a[4], b[4];
                std::memcpy(a, nativeKicks.bytes.data() + q * 16, 16);
                std::memcpy(b, ref->kicks.data() + q * 16, 16);
                std::fprintf(stderr, "  PATH1 qword %zu: native %08X %08X %08X %08X, reference %08X %08X %08X %08X\n", q, a[0], a[1], a[2],
                             a[3], b[0], b[1], b[2], b[3]);
                ++shown;
            }
            shown = 0;
            for (uint32_t q = 0; q < 870 && shown < 4; ++q) {
                if (!std::memcmp(native->data + q * 16, ref->data + q * 16, 16)) continue;
                uint32_t a[4], b[4];
                std::memcpy(a, native->data + q * 16, 16);
                std::memcpy(b, ref->data + q * 16, 16);
                std::fprintf(stderr, "  qword %u (TOP + %d): native %08X %08X %08X %08X, reference %08X %08X %08X %08X\n", q,
                             static_cast<int>(q) - static_cast<int>(top), a[0], a[1], a[2], a[3], b[0], b[1], b[2], b[3]);
                ++shown;
            }
        }
    };
    static const uint16_t blocks[15] = {0x060, 0x0C8, 0x240, 0x3C8, 0x560, 0x738, 0x910, 0xAF8, 0xC58, 0xE50, 0x1058, 0x1278, 0x14B8, 0x15B8, 0x16A8};
    static const Layout unpacks[6] = {Layout::IntegerNormalsInSt, Layout::IntegerNormalsInColour, Layout::Integers, Layout::Floats, Layout::Floats, Layout::Floats};
    for (int round = 0; round < rounds; ++round) {
        for (uint32_t group = 0; group < 24; ++group)
            for (uint32_t skip = 0; skip < 2; ++skip) {
                const uint32_t pc = (0x1B08 + group * 0x28 + skip * 8) / 8;
                const uint32_t top = r.chance(0.5f) ? 0u : 435u;
                if (group < 12) {
                    randomBatch(*vu, top, r, Layout::Floats, false);
                    check(pc, top, 0, "direct");
                    continue;
                }
                for (const uint16_t block : blocks)
                    for (uint32_t entry = 0; entry < 6; ++entry) {
                        const bool lit = block != 0x060 && block != 0x14B8 && block != 0x16A8;
                        randomBatch(*vu, top, r, unpacks[entry], lit);
                        check(pc, top, (block + entry * 0x10) / 8, "lighting");
                    }
            }
        for (int k = 0; k < 4; ++k) {
            const uint32_t top = r.chance(0.5f) ? 0u : 435u;
            randomGrid(*vu, top, r, true);
            check(0x3D50 / 8, top, 0, "grid ramps");
            randomGrid(*vu, top, r, false);
            check(0x3EA0 / 8, top, 0, "grid counters");
        }
    }
    std::printf("vu1_native_fuzz: %llu runs, %llu left to the interpreter (clipper record overflow), %llu failed\n", (unsigned long long)runs,
                (unsigned long long)notNative, (unsigned long long)failures);
    return failures ? 1 : 0;
}
