// The native renderer's GS memory model (platform/gsnative/gs_memory.cpp) against paraLLEl-GS: random
// rectangles uploaded in every pixel format and copied between formats through both, then the whole 4 MiB
// compared byte for byte. Needs a Vulkan device paraLLEl-GS accepts; skipped (exit code 77) without one.
#include "gs_backend.h"
#include "gs_memory.h"
#include "gs_packets.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace gt4::test;
using gt4::gsn::GsMemory;

namespace {

int failures = 0;

struct Format {
    uint32_t psm;
    const char* name;
};
const Format kFormats[] = {{0x00, "CT32"}, {0x01, "CT24"}, {0x02, "CT16"}, {0x0A, "CT16S"}, {0x13, "T8"},
                           {0x14, "T4"},   {0x1B, "T8H"},  {0x24, "T4HL"}, {0x2C, "T4HH"},  {0x30, "Z32"},
                           {0x31, "Z24"},  {0x32, "Z16"},  {0x3A, "Z16S"}};

// Compares the whole memory; prints the first differing words.
bool sameMemory(gt4::gs::Backend& gs, const GsMemory& model, const char* what) {
    std::vector<uint8_t> vram(GsMemory::kBytes);
    gs.readVram(0, vram.data(), vram.size());
    if (!std::memcmp(vram.data(), model.data(), vram.size())) return true;
    int shown = 0;
    for (size_t a = 0; a < vram.size() && shown < 6; a += 4) {
        uint32_t x, y;
        std::memcpy(&x, &vram[a], 4);
        std::memcpy(&y, model.data() + a, 4);
        if (x == y) continue;
        std::fprintf(stderr, "  %s: byte %#zx: paraLLEl-GS %08X, model %08X\n", what, a, x, y);
        ++shown;
    }
    std::fprintf(stderr, "FAILED: %s\n", what);
    ++failures;
    return false;
}

}  // namespace

int main() {
    gt4::gs::BackendOptions options;
    std::string error;
    auto gs = gt4::gs::Backend::create(options, error);
    if (!gs) {
        std::printf("gs_native: no suitable Vulkan device (%s); skipped\n", error.c_str());
        return 77;
    }
    std::vector<uint8_t> zero(GsMemory::kBytes, 0);
    gs->writeVram(0, zero.data(), zero.size());
    auto model = std::make_unique<GsMemory>();
    std::mt19937 rng(7);
    auto random = [&](uint32_t lo, uint32_t hi) { return std::uniform_int_distribution<uint32_t>(lo, hi)(rng); };

    // Uploads: each format, several rectangles at random block addresses, widths and positions.
    for (const Format& f : kFormats) {
        gs->writeVram(0, zero.data(), zero.size());
        std::memset(model->data(), 0, GsMemory::kBytes);
        for (int round = 0; round < 3; ++round) {
            GsMemory::Transfer t;
            t.dpsm = f.psm;
            t.dbp = random(0, 0x3000) & ~31u;          // page-aligned and not
            if (round == 1) t.dbp |= random(1, 31);
            t.dbw = (f.psm == 0x13 || f.psm == 0x14) ? 2 * random(1, 5) : random(1, 10);
            // Inside the buffer's width (a rectangle that crosses it is undefined).
            const uint32_t width = 64 * t.dbw;
            t.rrw = 8 * random(1, std::min<uint32_t>(16, width / 8));
            t.dsax = random(0, width - t.rrw);
            t.dsay = random(0, 40);
            t.rrh = random(1, 70);
            // paraLLEl-GS loses the byte alignment of a 24-bit upload's stream unless the upload is small
            // (a few KiB), starts at the origin of a page-aligned buffer and stays in one page column; the GS
            // reads the stream continuously, as the model does. GT4 never uploads 24-bit data (it renders
            // into its 24-bit frame).
            if (gt4::gsn::psmBits(f.psm) == 24) {
                t.dsax = t.dsay = 0;
                t.dbp &= ~31u;
                t.rrw = std::min<uint32_t>(t.rrw, 64);
                t.rrh = std::min<uint32_t>(t.rrh, 20);
                if ((t.rrw / 8) * t.rrh % 2) ++t.rrh;   // a whole number of qwords
            }
            const size_t bytes = size_t(t.rrw) * t.rrh * gt4::gsn::psmBits(f.psm) / 8;
            std::vector<uint8_t> data(bytes);
            for (auto& b : data) b = static_cast<uint8_t>(rng());
            Gif g;
            g.ad({{BITBLTBUF, bitbltbuf(t.dbp, t.dbw, t.dpsm)}, {TRXPOS, trxpos(t.dsax, t.dsay)}, {TRXREG, trxreg(t.rrw, t.rrh)}, {TRXDIR, 0}}, false);
            g.image(data.data(), data.size());
            gs->gif(3, g.data(), g.bytes());
            model->beginWrite(t);
            model->write(data.data(), data.size());
            char what[160];
            std::snprintf(what, sizeof what, "upload %s: DBP %#x DBW %u at (%u, %u), %u x %u", f.name, t.dbp, t.dbw, t.dsax, t.dsay, t.rrw, t.rrh);
            if (!sameMemory(*gs, *model, what)) break;
        }
    }
    // Local-to-local copies between formats of the same depth (from memory filled with random words).
    std::vector<uint8_t> noise(GsMemory::kBytes);
    for (auto& b : noise) b = static_cast<uint8_t>(rng());
    gs->writeVram(0, noise.data(), noise.size());
    std::memcpy(model->data(), noise.data(), noise.size());
    const uint32_t pairs[][2] = {{0x00, 0x00}, {0x00, 0x30}, {0x02, 0x0A}, {0x13, 0x13}, {0x14, 0x14}, {0x01, 0x31}};
    for (const auto& pair : pairs) {
        GsMemory::Transfer t;
        t.spsm = pair[0];
        t.dpsm = pair[1];
        t.sbp = random(0, 0x2000);
        t.dbp = random(0x2000, 0x3800);
        t.sbw = t.dbw = (pair[0] == 0x13 || pair[0] == 0x14) ? 4 : random(1, 8);
        t.rrw = 8 * random(1, 8);
        t.ssax = random(0, 64 * t.sbw - t.rrw);
        t.ssay = random(0, 30);
        t.dsax = random(0, 64 * t.dbw - t.rrw);
        t.dsay = random(0, 30);
        t.rrh = random(1, 40);
        Gif g;
        g.ad({{BITBLTBUF, bitbltbuf(t.dbp, t.dbw, t.dpsm, t.sbp, t.sbw, t.spsm)}, {TRXPOS, trxpos(t.dsax, t.dsay, t.ssax, t.ssay)},
              {TRXREG, trxreg(t.rrw, t.rrh)}, {TRXDIR, 2}});
        gs->gif(3, g.data(), g.bytes());
        model->copy(t);
        sameMemory(*gs, *model, ("copy " + std::to_string(pair[0]) + " -> " + std::to_string(pair[1])).c_str());
    }
    std::printf("gs_native: %s\n", failures ? "FAILED" : "memory model matches paraLLEl-GS");
    return failures ? 1 : 0;
}
