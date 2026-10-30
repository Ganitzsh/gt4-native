// The VU1 reference beside the native 3D path (GT4_VU1_INTERPRETER builds; see vu1_reference.h).
#include "vu1_reference.h"

#include "vu1_inventory.h"
#include "vu1_ref.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace gt4::ps2 {

namespace {

// Collects GIF packets (a reference's XGKICKs) as events.
struct PacketEvents final : GifSink {
    std::vector<Vu1Reference::Event> events;
    void gif(uint32_t, const uint8_t* data, size_t bytes) override { Vu1Reference::decodeGif(data, bytes, events); }
};

// Collects the native renderers' output as events, and keeps it to be drawn afterwards.
struct NativeOutput final : PrimitiveSink {
    std::vector<Vu1Reference::Event> events;
    struct Item {
        std::vector<uint8_t> packet;
        GsPrimitives prims;
        std::vector<GsVertex> vertices;
    };
    std::vector<Item> items;
    void packet(const uint8_t* gif, size_t bytes) override {
        Vu1Reference::decodeGif(gif, bytes, events);
        items.push_back({std::vector<uint8_t>(gif, gif + bytes), {}, {}});
    }
    void primitives(const GsPrimitives& p) override {
        if (p.setPrim) events.push_back({false, 0x00, p.prim, {}, false});
        for (size_t k = 0; k < p.count; ++k) events.push_back({true, 0, 0, p.vertices[k], p.fog});
        Item item;
        item.prims = p;
        item.vertices.assign(p.vertices, p.vertices + p.count);
        items.push_back(std::move(item));
    }
    void replay(PrimitiveSink& out) {
        for (Item& item : items) {
            if (!item.packet.empty()) {
                out.packet(item.packet.data(), item.packet.size());
            } else {
                item.prims.vertices = item.vertices.data();
                out.primitives(item.prims);
            }
        }
    }
};

// Passes a reference's kicks on to the GS as packets.
struct ToSink final : GifSink {
    PrimitiveSink& out;
    explicit ToSink(PrimitiveSink& o) : out(o) {}
    void gif(uint32_t, const uint8_t* data, size_t bytes) override { out.packet(data, bytes); }
};

uint32_t lightingBody(uint32_t itop) {
    static const uint16_t blocks[15] = {0x060, 0x0C8, 0x240, 0x3C8, 0x560, 0x738, 0x910, 0xAF8, 0xC58, 0xE50, 0x1058, 0x1278, 0x14B8, 0x15B8, 0x16A8};
    const uint32_t at = itop * 8;
    for (const uint16_t start : blocks)
        if (at >= start && at <= start + 0x50u && (at - start) % 0x10 == 0) return start + 0x50u;
    return 0;
}

}  // namespace

Vu1Reference::Vu1Reference(Vif1& v) : vif(v), ref(std::make_unique<Vu1Ref>()) {
    vif.observer = this;
    auto parse = [](const char* e, Mode fallback) {
        if (!e) return fallback;
        if (!std::strcmp(e, "interp")) return Mode::Interp;
        if (!std::strcmp(e, "ref")) return Mode::Ref;
        if (!std::strcmp(e, "native")) return Mode::Native;
        return Mode::Exact;
    };
    mode = parse(std::getenv("GT4_VU1"), Mode::Native);
    const char* d = std::getenv("GT4_VU1_DIFF");
    diff = d && std::strcmp(d, "0") ? parse(d, Mode::Exact) : Mode::Native;
    detail = std::getenv("GT4_VU1_DIFF_DETAIL") != nullptr;
    inventory = Vu1Inventory::get() != nullptr;
    if (inventory) mode = Mode::Interp;
}

Vu1Reference::~Vu1Reference() {
    vif.observer = nullptr;
    if (counts.empty()) return;
    std::fprintf(stderr, "[vu1 diff] entry/body: runs, identical, within rounding, different; vertices (changed, beyond rounding, of those drawn, wrapped); max |dxy| (1/16 px), |dz|, colour, fog, ST (relative)\n");
    for (const auto& [key, c] : counts)
        std::fprintf(stderr, "[vu1 diff] %04X/%04X: %llu %llu %llu %llu; %llu (%llu, %llu, %llu, %llu wrapped); %u %u %u %u %.2g\n", key >> 16,
                     key & 0xFFFF, (unsigned long long)c.runs, (unsigned long long)c.identical, (unsigned long long)c.close,
                     (unsigned long long)c.differ, (unsigned long long)c.vertices, (unsigned long long)c.verticesChanged,
                     (unsigned long long)c.far, (unsigned long long)c.farDrawn, (unsigned long long)c.wraps, c.maxXy, c.maxZ, c.maxColour,
                     c.maxFog, c.maxSt);
}

void Vu1Reference::restore(const uint8_t* data, const uint32_t vf[32][4], const std::vector<uint8_t>& micro) {
    std::memcpy(unit.data, data, sizeof unit.data);
    std::memcpy(unit.vf, vf, sizeof unit.vf);
    std::memcpy(unit.micro, micro.data(), std::min(micro.size(), sizeof unit.micro));
    exact.microWritten();
}

void Vu1Reference::unpacked(uint32_t address, const uint32_t value[4], unsigned lanes) {
    uint32_t* q = reinterpret_cast<uint32_t*>(unit.data + (address & 0x3FF) * 16);
    for (int i = 0; i < 4; ++i)
        if (lanes & (1u << i)) q[i] = value[i];
}

void Vu1Reference::mpg(uint32_t address, const uint32_t* words, uint32_t instructions) {
    for (uint32_t k = 0; k < instructions; ++k) std::memcpy(unit.micro + ((address + k) & 0x7FF) * 8, words + 2 * k, 8);
    exact.microWritten();
    if (Vu1Inventory* inv = Vu1Inventory::get()) inv->microWritten();
}

uint32_t Vu1Reference::runReference(const Batch3d& batch, Mode how, GifSink* sink) {
    unit.top = batch.top;
    unit.itop = batch.itop;
    GifSink* previous = unit.sink();
    unit.setGifSink(sink);
    Vu1Inventory* inv = Vu1Inventory::get();
    if (how == Mode::Exact && exact.run(unit, batch.entry)) {
    } else if (how == Mode::Ref) {
        ref->load(unit);
        ref->sink = sink;
        ref->run(batch.entry);
        ref->store(unit);
    } else {
        if (inv) inv->before(unit, batch.entry);
        const uint64_t cycles = unit.run(batch.entry);
        if (inv) inv->after(unit, cycles);
    }
    unit.setGifSink(previous);
    return unit.pc();
}

bool Vu1Reference::run(const Batch3d& batch, Renderers3d& renderers, PrimitiveSink& out, uint32_t& continueAt, bool gt4Program) {
    if (!gt4Program || mode != Mode::Native) {   // drawn by the reference
        ToSink sink(out);
        continueAt = runReference(batch, gt4Program ? mode : Mode::Interp, &sink);
        return true;
    }
    if (diff == Mode::Native) {
        // Keep the model's state as the programs leave it, for a later switch: run the exact port silently.
        runReference(batch, Mode::Exact, nullptr);
        return false;
    }
    // GT4_VU1_DIFF_DUMP=ENTRY (hex byte address): the inputs of the first batch of that entry whose outputs
    // differ, as the batch buffer and as VU memory have them (before the run).
    static const long dumpEntry = std::getenv("GT4_VU1_DIFF_DUMP") ? std::strtol(std::getenv("GT4_VU1_DIFF_DUMP"), nullptr, 16) : -1;
    std::vector<uint8_t> before;
    if (dumpEntry == static_cast<long>(batch.entry * 8)) before.assign(unit.data, unit.data + sizeof unit.data);
    PacketEvents reference;
    runReference(batch, diff, &reference);
    NativeOutput native;
    renderers.run(batch, native, continueAt);
    const uint64_t differBefore = counts[(batch.entry * 8) << 16 | lightingBody(batch.itop)].differ;
    compare(batch, reference.events, native.events);
    if (!before.empty() && counts[(batch.entry * 8) << 16 | lightingBody(batch.itop)].differ != differBefore) {
        static bool dumped = false;
        if (!dumped) {
            dumped = true;
            for (uint32_t q = 0; q < 242; ++q) {
                const uint32_t* b = batch.buffer->qword(q);
                const uint32_t* m = reinterpret_cast<const uint32_t*>(before.data() + ((batch.top + q) & 0x3FF) * 16);
                std::fprintf(stderr, "[vu1 dump] %3u buffer %08X %08X %08X %08X  vu %08X %08X %08X %08X%s\n", q, b[0], b[1], b[2], b[3], m[0], m[1],
                             m[2], m[3], std::memcmp(b, m, 16) ? "  *" : "");
            }
        }
    }
    native.replay(out);
    return true;
}

void Vu1Reference::decodeGif(const uint8_t* data, size_t bytes, std::vector<Event>& out) {
    const size_t n = bytes / 16;
    size_t q = 0;
    float s = 0, t = 0, qv = 1;
    uint32_t rgba = 0;
    while (q < n) {
        uint64_t lo, hi;
        std::memcpy(&lo, data + q * 16, 8);
        std::memcpy(&hi, data + q * 16 + 8, 8);
        ++q;
        const uint32_t nloop = lo & 0x7FFF, flg = (lo >> 58) & 3;
        uint32_t nreg = (lo >> 60) & 15;
        if (!nreg) nreg = 16;
        if (flg == 0 && ((lo >> 46) & 1)) out.push_back({false, 0x00, (lo >> 47) & 0x7FF, {}, false});
        if (flg == 0) {
            for (uint32_t l = 0; l < nloop; ++l)
                for (uint32_t r = 0; r < nreg && q < n; ++r, ++q) {
                    const uint32_t reg = (hi >> (4 * r)) & 15;
                    uint32_t w[4];
                    std::memcpy(w, data + q * 16, 16);
                    const uint64_t a = w[0] | static_cast<uint64_t>(w[1]) << 32;
                    switch (reg) {
                    case 0x1: rgba = (w[0] & 0xFF) | (w[1] & 0xFF) << 8 | (w[2] & 0xFF) << 16 | (w[3] & 0xFF) << 24; break;
                    case 0x2:
                        std::memcpy(&s, &w[0], 4);
                        std::memcpy(&t, &w[1], 4);
                        std::memcpy(&qv, &w[2], 4);
                        break;
                    case 0x4: case 0xC: case 0x5: case 0xD: {
                        Event e;
                        e.vertex = true;
                        e.fog = reg == 0x4 || reg == 0xC;
                        e.v = {s, t, qv, rgba, w[0] & 0xFFFF, w[1] & 0xFFFF, e.fog ? (w[2] >> 4) & 0xFFFFFF : w[2], e.fog ? (w[3] >> 4) & 0xFF : 0,
                               reg >= 0xC || ((w[3] >> 15) & 1)};
                        out.push_back(e);
                        break;
                    }
                    case 0xE: out.push_back({false, w[2] & 0xFF, a, {}, false}); break;
                    case 0xF: break;
                    default: out.push_back({false, reg, a, {}, false}); break;
                    }
                }
        } else if (flg == 1) {
            const uint32_t words = nloop * nreg;
            for (uint32_t k = 0; k < words && q + k / 2 < n; ++k) {
                uint64_t v;
                std::memcpy(&v, data + (q + k / 2) * 16 + (k & 1) * 8, 8);
                out.push_back({false, static_cast<uint32_t>((hi >> (4 * (k % nreg))) & 15), v, {}, false});
            }
            q += (words + 1) / 2;
        } else {
            out.push_back({false, 0x54, nloop, {}, false});   // an image transfer of NLOOP qwords
            q += nloop;
        }
    }
}

void Vu1Reference::compare(const Batch3d& batch, const std::vector<Event>& a, const std::vector<Event>& b) {
    Count& c = counts[(batch.entry * 8) << 16 | lightingBody(batch.itop)];
    char label[32];
    std::snprintf(label, sizeof label, "%04X/%04X", batch.entry * 8, lightingBody(batch.itop));
    size_t first = 0;
    const Verdict verdict = classify(a, b, c, first, detail ? label : nullptr);
    if (verdict != Verdict::Differ || shown >= 12 || !detail) return;
    ++shown;
    std::fprintf(stderr, "[vu1 diff] entry %04X body %04X top %u: %zu reference events, %zu native; first difference at %zu\n", batch.entry * 8,
                 lightingBody(batch.itop), batch.top, a.size(), b.size(), first);
    for (size_t k = first; k < std::min<size_t>(std::max(a.size(), b.size()), first + 4); ++k)
        for (const auto* list : {&a, &b}) {
            if (k >= list->size()) continue;
            const Event& e = (*list)[k];
            if (e.vertex)
                std::fprintf(stderr, "   %s %zu: vertex x %u y %u z %u f %u adc %d rgba %08X stq %g %g %g\n", list == &a ? "ref" : "nat", k, e.v.x, e.v.y,
                             e.v.z, e.v.fog, e.v.adc, e.v.rgba, e.v.s, e.v.t, e.v.q);
            else
                std::fprintf(stderr, "   %s %zu: reg %02X = %016llX\n", list == &a ? "ref" : "nat", k, e.reg, (unsigned long long)e.value);
        }
}

Vu1Reference::Verdict Vu1Reference::classify(const std::vector<Event>& a, const std::vector<Event>& b, Count& c, size_t& firstOut, const char* printFar, bool colours) {
    static uint64_t shownFar = 0;
    ++c.runs;
    bool identical = a.size() == b.size(), close = identical;
    size_t first = ~size_t(0);
    for (size_t k = 0; k < std::min(a.size(), b.size()); ++k) {
        const Event& x = a[k];
        const Event& y = b[k];
        if (x.vertex != y.vertex) {
            identical = close = false;
            first = std::min(first, k);
            break;
        }
        if (!x.vertex) {
            if (x.reg != y.reg || x.value != y.value) {
                identical = close = false;
                first = std::min(first, k);
            }
            continue;
        }
        ++c.vertices;
        const GsVertex& u = x.v;
        const GsVertex& v = y.v;
        const bool same = (u.rgba == v.rgba || !colours) && u.s == v.s && u.t == v.t && u.q == v.q && u.rgba == v.rgba && u.x == v.x && u.y == v.y && u.z == v.z && u.fog == v.fog &&
                          u.adc == v.adc && x.fog == y.fog;
        if (same) continue;
        ++c.verticesChanged;
        identical = false;
        auto d = [](uint32_t p, uint32_t q) { return p > q ? p - q : q - p; };
        const uint32_t dxy = std::max(d(u.x, v.x), d(u.y, v.y)), dz = d(u.z, v.z), dfog = d(u.fog, v.fog);
        uint32_t dc = 0;
        for (int i = 0; i < 4; ++i) dc = std::max(dc, d((u.rgba >> (8 * i)) & 0xFF, (v.rgba >> (8 * i)) & 0xFF));
        auto rel = [](float p, float q) {
            const float m = std::max(std::fabs(p), std::fabs(q));
            return static_cast<double>(std::fabs(p - q) / std::max(m, 1e-3f));
        };
        const double dst = std::max({rel(u.s, v.s), rel(u.t, v.t), rel(u.q, v.q)});
        // A coordinate on either side of 0 (x, y far outside the screen, Z at the far plane) wraps: 0 and 0xFFFF.
        const bool wrap = dxy > 0x8000 || dz > 0x800000;
        if (wrap) ++c.wraps;
        else {
            c.maxXy = std::max(c.maxXy, dxy);
            c.maxZ = std::max(c.maxZ, dz);
        }
        c.maxColour = std::max(c.maxColour, dc);
        c.maxFog = std::max(c.maxFog, dfog);
        c.maxSt = std::max(c.maxSt, dst);
        // Rounding: an eighth of a pixel, and 1/4096 of the distance from the window's centre (points far outside
        // the screen are far from 0 in clip space too); Z by 64 (GT4's viewport puts Z near 2^28 before its
        // offset, where a float's step is 32) and 1/8192 of Z; colour by two steps; ST by 1e-3.
        auto far = [](uint32_t c) { return (c > 0x8000 ? c - 0x8000 : 0x8000 - c) / 4096; };
        const uint32_t xyLimit = 2 + std::max(far(u.x), far(u.y)), zLimit = 64 + std::max(u.z, v.z) / 8192;
        const bool near = !wrap && u.adc == v.adc && dxy <= xyLimit && dz <= zLimit && (dc <= 2 || !colours) && dfog <= 1 && dst <= 1e-3;
        if (!near) {
            close = false;
            first = std::min(first, k);
            ++c.far;
            if (!u.adc || !v.adc) ++c.farDrawn;
            if (printFar && shownFar < 40 && (!u.adc || !v.adc)) {
                ++shownFar;
                std::fprintf(stderr, "[vu1 far] %s ev %zu/%zu: ref x %u y %u z %u f %u adc %d rgba %08X stq %g %g %g | nat x %u y %u z %u f %u adc %d rgba %08X stq %g %g %g\n",
                             printFar, k, a.size(), u.x, u.y, u.z, u.fog, u.adc, u.rgba, u.s, u.t, u.q, v.x, v.y, v.z, v.fog, v.adc,
                             v.rgba, v.s, v.t, v.q);
            }
        }
    }
    if (identical) ++c.identical;
    else if (close) ++c.close;
    else ++c.differ;
    firstOut = first == ~size_t(0) ? 0 : first;
    return identical ? Verdict::Identical : close ? Verdict::Close : Verdict::Differ;
}

}  // namespace gt4::ps2
