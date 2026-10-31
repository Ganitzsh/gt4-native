// Game -> host graphics stream dispatch (see stream.h).
#include "stream.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

#if GT4_VU1_INTERPRETER
#include "vu1_reference.h"
#endif

namespace gt4::ps2 {

#if GT4_VU1_INTERPRETER
struct GraphicsStream::Reference {
    Vu1Reference tool;
    explicit Reference(Vif1& vif) : tool(vif) {}
};
#endif

GraphicsStream::GraphicsStream(GsTarget& target) : gs(target) {
    sink.owner = this;
    rendererSink.owner = this;
    vif1.setGifSink(&sink);
    vif1.onPath3Mask = [this](bool masked) { if (!masked) releasePath3(); };
    vif1.onBatch = [this](const Batch3d& batch) { runBatch(batch); };
    vif1.onRecords = [this](const uint8_t* data, size_t bytes) { records(data, bytes); };
#if GT4_VU1_INTERPRETER
    if (Vu1Reference::wanted()) reference = std::make_unique<Reference>(vif1);
#endif
    if (const char* e = std::getenv("GT4_VRAM_DUMP")) {
        dumpFrame = static_cast<uint32_t>(std::strtoul(e, nullptr, 10));
        if (const char* list = std::strchr(e, ':'))
            for (const char* p = list + 1; *p; ++p) {
                if (std::strncmp(p, "end", 3) == 0) dumpAtEnd = true;
                else dumpAt.push_back(static_cast<uint32_t>(std::strtoul(p, nullptr, 10)));
                p = std::strchr(p, ',');
                if (!p) break;
            }
    }
    if (const char* e = std::getenv("GT4_GIF_LOG")) logFrame = static_cast<uint32_t>(std::strtoul(e, nullptr, 10));
}

GraphicsStream::~GraphicsStream() = default;

void GraphicsStream::logDirect(const uint8_t* data, size_t bytes) {
    static long left = std::getenv("GT4_DIRECT_LOG") ? std::strtol(std::getenv("GT4_DIRECT_LOG"), nullptr, 10) : 0;
    if (left <= 0 || bytes < 16) return;
    --left;
    uint64_t tag[2], first[2] = {};
    std::memcpy(tag, data, 16);
    if (bytes >= 32) std::memcpy(first, data + 16, 16);
    std::printf("[direct] vblank %u: %zu bytes, tag %016llX regs %016llX, then %016llX %016llX\n", lastVblank, bytes,
                static_cast<unsigned long long>(tag[0]), static_cast<unsigned long long>(tag[1]),
                static_cast<unsigned long long>(first[0]), static_cast<unsigned long long>(first[1]));
    for (size_t q = 2; q < std::min<size_t>(bytes / 16, 12); ++q) {
        uint64_t w[2];
        std::memcpy(w, data + q * 16, 16);
        std::printf("[direct]   %016llX %016llX\n", static_cast<unsigned long long>(w[0]), static_cast<unsigned long long>(w[1]));
    }
}

// Native records (include/gt4/gfx.h): headers and payloads, each padded to 16 bytes.
void GraphicsStream::records(const uint8_t* data, size_t bytes) {
    size_t at = 0;
    while (at + sizeof(gfx::Header) <= bytes) {
        gfx::Header h;
        std::memcpy(&h, data + at, sizeof h);
        const uint8_t* payload = data + at + sizeof h;
        if (at + sizeof h + h.bytes() > bytes) {
            vif1.lastWarning = "a native record past the end of its block";
            ++counters.badRecords;
            static int logged = 0;
            if (logged++ < 10)
            {
                std::fprintf(stderr, "GS: bad record (vblank %u): type %u, %u bytes at %zu of a %zu-byte block\n", lastVblank, h.type(),
                             h.bytes(), at, bytes);
                for (size_t q = (at >= 64 ? at - 64 : 0) & ~size_t(15); q < std::min(bytes, at + 64); q += 16) {
                    uint32_t w[4];
                    std::memcpy(w, data + q, 16);
                    std::fprintf(stderr, "GS:   %6zu: %08X %08X %08X %08X\n", q, w[0], w[1], w[2], w[3]);
                }
            }
            return;
        }
        switch (h.type()) {
        case gfx::kNop: break;
        case gfx::kState:
            if (h.bytes() >= sizeof(gfx::StateRecord)) {
                gfx::StateRecord r;
                std::memcpy(&r, payload, sizeof r);
                applyNative(r);
            } else {
                ++counters.badRecords;
            }
            break;
        case gfx::kUpload:
            if (h.bytes() >= sizeof(gfx::UploadRecord)) {
                gfx::UploadRecord u;
                std::memcpy(&u, payload, sizeof u);
                auto it = std::find_if(recordData.begin(), recordData.end(), [&](const RecordData& d) { return d.source == u.source; });
                ++counters.uploads;
                if (it == recordData.end()) {
                    ++counters.uploadsWithoutData;
                    gs.upload(u, nullptr, 0);
                } else {
                    gs.upload(u, it->bytes.data(), std::min<size_t>(it->bytes.size(), size_t(u.qwords) * 16));
                    recordData.erase(it);
                }
            } else {
                ++counters.badRecords;
            }
            break;
        case gfx::kBatchFrame:
            vif1.batchFrame();
            break;
        case gfx::kBatchWrite: {
            gfx::BatchWrite w;
            if (h.bytes() < sizeof w) {
                ++counters.badRecords;
                break;
            }
            std::memcpy(&w, payload, sizeof w);
            if (h.bytes() < sizeof w + size_t(w.count) * 16) {
                ++counters.badRecords;
                break;
            }
            std::vector<uint32_t> words(size_t(w.count) * 4);
            if (w.count) std::memcpy(words.data(), payload + sizeof w, size_t(w.count) * 16);
            vif1.batchWrite(w.qword, words.data(), w.count, w.lanes);
            break;
        }
        case gfx::kBatchModel: {
            gfx::BatchModel m;
            if (h.bytes() < sizeof m) {
                ++counters.badRecords;
                break;
            }
            std::memcpy(&m, payload, sizeof m);
            auto it = std::find_if(recordData.begin(), recordData.end(), [&](const RecordData& d) { return d.source == m.source; });
            if (it == recordData.end()) {
                ++counters.uploadsWithoutData;
                break;
            }
            const uint64_t unknownBefore = vif1.stats.unknownCodes;
            vif1.batchModel(it->bytes.data(), std::min<size_t>(it->bytes.size(), size_t(m.qwords) * 16), m.mask);
            if (vif1.stats.unknownCodes != unknownBefore) {
                static int logged = 0;
                if (logged++ < 6) {
                    std::fprintf(stderr, "GS: model packet at %08X (%u qwords, mask %08X, vblank %u) decodes badly\n", m.source, m.qwords, m.mask,
                                 lastVblank);
                    for (size_t q = 0; q < 6 && q * 16 < it->bytes.size(); ++q) {
                        uint32_t w[4];
                        std::memcpy(w, it->bytes.data() + q * 16, 16);
                        std::fprintf(stderr, "GS:   %08X %08X %08X %08X\n", w[0], w[1], w[2], w[3]);
                    }
                }
            }
            recordData.erase(it);
            break;
        }
        case gfx::kBatchRun: {
            gfx::BatchRun r;
            if (h.bytes() < sizeof r) {
                ++counters.badRecords;
                break;
            }
            std::memcpy(&r, payload, sizeof r);
            vif1.batchRun(r.entry, r.itop);
            break;
        }
        case gfx::kRead:
            if (h.bytes() >= sizeof(gfx::ReadRecord)) {
                gfx::ReadRecord r;
                std::memcpy(&r, payload, sizeof r);
                gs.read(r);
            } else {
                ++counters.badRecords;
            }
            break;
        case gfx::kDraw: {
            gfx::DrawRecord d;
            if (h.bytes() < sizeof d) {
                ++counters.badRecords;
                break;
            }
            std::memcpy(&d, payload, sizeof d);
            if (h.bytes() < sizeof d + size_t(d.count) * sizeof(gfx::Vertex)) {
                ++counters.badRecords;
                break;
            }
            std::vector<gfx::Vertex>& v = drawVertices;
            v.resize(d.count);
            if (d.count) std::memcpy(v.data(), payload + sizeof d, size_t(d.count) * sizeof(gfx::Vertex));
            gs.draw(d, v.data());
            ++counters.draws;
            break;
        }
        default: {
            ++counters.badRecords;
            static int logged = 0;
            if (logged++ < 10)
                std::fprintf(stderr, "GS: unknown record (vblank %u): type %u, %u bytes at %zu of a %zu-byte block\n", lastVblank, h.type(),
                             h.bytes(), at, bytes);
            break;
        }
        }
        ++counters.gfxRecords;
        at += size_t(gfx::recordQwords(h.bytes())) * 16;
    }
}

// A batch for GT4's 3D renderers (VIF1 MSCAL).
void GraphicsStream::runBatch(const Batch3d& batch) {
    uint32_t continueAt = vif1.continueAt;
#if GT4_VU1_INTERPRETER
    if (reference && reference->tool.run(batch, renderers, rendererSink, continueAt, vif1.gt4Program())) {
        vif1.continueAt = continueAt;
        return;
    }
#endif
    if (!vif1.gt4Program()) {   // another game's (or a test's) VU1 programs: nothing to draw them with
        ++counters.unknownPrograms;
        vif1.lastWarning = "a VU1 program image that is not GT4's (nothing drawn)";
        return;
    }
    if (!renderers.run(batch, rendererSink, continueAt))
        vif1.lastWarning = "no 3D renderer at entry " + std::to_string(batch.entry * 8);
    vif1.continueAt = continueAt;
}

void GraphicsStream::sendPrimitives(const GsPrimitives& p) {
    countPacket();
    ++counters.primitiveRuns;
    counters.primitiveVertices += p.count;
    if (frameIndex == logFrame)
        std::printf("primitives %u: PRIM %#x (%s), %zu vertices\n", packetIndex - 1, p.prim, p.setPrim ? "set" : "kept", p.count);
    // The registers they leave, for saveState.
    if (p.setPrim) shadowReg(0x00, p.prim & 0x7FF);
    if (p.count) {
        const GsVertex& v = p.vertices[p.count - 1];
        uint32_t s, t, q;
        std::memcpy(&s, &v.s, 4);
        std::memcpy(&t, &v.t, 4);
        std::memcpy(&q, &v.q, 4);
        shadowReg(0x02, s | static_cast<uint64_t>(t) << 32);
        shadowReg(0x01, v.rgba | static_cast<uint64_t>(q) << 32);
        packedQ = q;
        if (p.fog) shadowReg(0x0A, static_cast<uint64_t>(v.fog & 0xFF) << 56);
    }
    gs.primitives(p);
}

void GraphicsStream::dumpVram(const char* tag) {
    std::vector<uint8_t> vram(4u << 20);
    gs.readVram(vram.data());
    char name[64];
    std::snprintf(name, sizeof name, "vram_%u_%s.bin", frameIndex, tag);
    if (FILE* f = std::fopen(name, "wb")) {
        std::fwrite(vram.data(), 1, vram.size(), f);
        std::fclose(f);
        std::printf("GS memory written to %s\n", name);
    }
}

// GT4_VRAM_DUMP counts what goes to the GS: GIF packets and runs of primitives.
void GraphicsStream::countPacket() {
    if (frameIndex == dumpFrame && std::find(dumpAt.begin(), dumpAt.end(), packetIndex) != dumpAt.end())
        dumpVram(std::to_string(packetIndex).c_str());
    ++packetIndex;
}

void GraphicsStream::sendGif(uint32_t path, const uint8_t* data, size_t bytes) {
    countPacket();
    if (frameIndex == logFrame) {
        uint64_t tag[2] = {};
        if (bytes >= 16) std::memcpy(tag, data, 16);
        std::printf("GIF %u: path %u, %zu bytes, tag %016llx %016llx, program %#x\n", packetIndex - 1, path, bytes,
                    static_cast<unsigned long long>(tag[1]), static_cast<unsigned long long>(tag[0]),
                    path == 1 ? vif1.lastProgram : 0);
        // and the packets themselves, to gif_FRAME.bin: u32 path (and the VU1 program << 8 for
        // PATH1), u32 bytes, data
        static FILE* out = nullptr;
        if (!out) {
            char name[64];
            std::snprintf(name, sizeof name, "gif_%u.bin", frameIndex);
            out = std::fopen(name, "wb");
        }
        if (out) {
            const uint32_t head[2] = {path | (path == 1 ? vif1.lastProgram << 8 : 0), static_cast<uint32_t>(bytes)};
            std::fwrite(head, 4, 2, out);
            std::fwrite(data, 1, bytes, out);
            std::fflush(out);
        }
    }
    shadowGif(path, data, bytes);
    gs.gif(path, data, bytes);
}

// Follows the registers a GIF packet writes (for saveState). A packet may continue in the next one of its path.
void GraphicsStream::shadowGif(uint32_t path, const uint8_t* data, size_t bytes) {
    GifShadow& g = gifShadow[path & 3];
    const size_t n = bytes / 16;
    size_t q = 0;
    while (q < n) {
        uint64_t lo, hi;
        std::memcpy(&lo, data + q * 16, 8);
        std::memcpy(&hi, data + q * 16 + 8, 8);
        if (!g.left) {   // a tag
            ++q;
            const uint32_t nloop = lo & 0x7FFF;
            g.flg = (lo >> 58) & 3;
            g.nreg = (lo >> 60) & 15;
            if (!g.nreg) g.nreg = 16;
            g.regs = hi;
            g.reg = 0;
            g.left = g.flg >= 2 ? nloop : nloop * g.nreg;
            if ((lo >> 46) & 1) shadowReg(0x00, (lo >> 47) & 0x7FF);   // PRE: PRIM
            continue;
        }
        if (g.flg == 0) {   // PACKED
            const uint32_t r = (g.regs >> (4 * g.reg)) & 15;
            switch (r) {
            case 0x0: shadowReg(0x00, lo & 0x7FF); break;
            case 0x1:
                shadowReg(0x01, (lo & 0xFF) | ((lo >> 32) & 0xFF) << 8 | (hi & 0xFF) << 16 | ((hi >> 32) & 0xFF) << 24 |
                                    static_cast<uint64_t>(packedQ) << 32);
                break;
            case 0x2: shadowReg(0x02, lo); packedQ = static_cast<uint32_t>(hi); break;
            case 0x3: shadowReg(0x03, (lo & 0x3FFF) | ((lo >> 32) & 0x3FFF) << 16); break;
            case 0x4: case 0xC: shadowReg(0x0A, ((hi >> 36) & 0xFF) << 56); break;   // XYZF: its F is FOG
            case 0xA: shadowReg(0x0A, ((hi >> 36) & 0xFF) << 56); break;
            case 0xE: shadowReg(static_cast<uint32_t>(hi & 0xFF), lo); break;
            case 0x5: case 0xD: case 0xF: break;
            default: shadowReg(r, lo); break;   // TEX0_1/2, CLAMP_1/2
            }
            g.reg = (g.reg + 1) % g.nreg;
            --g.left;
            ++q;
        } else if (g.flg == 1) {   // REGLIST: two registers per qword (the descriptors are the addresses)
            for (uint64_t word : {lo, hi}) {
                if (!g.left) break;
                const uint32_t r = (g.regs >> (4 * g.reg)) & 15;
                if (r <= 0xD) shadowReg(r, word);
                g.reg = (g.reg + 1) % g.nreg;
                --g.left;
            }
            ++q;
        } else {   // IMAGE
            const size_t skip = std::min<size_t>(g.left, n - q);
            q += skip;
            g.left -= static_cast<uint32_t>(skip);
        }
    }
}

namespace {
struct StateWriter {
    std::vector<uint8_t>& out;
    void bytes(const void* p, size_t n) { out.insert(out.end(), static_cast<const uint8_t*>(p), static_cast<const uint8_t*>(p) + n); }
    void u32(uint32_t v) { bytes(&v, 4); }
    void u64(uint64_t v) { bytes(&v, 8); }
};
struct StateReader {
    const uint8_t* p;
    size_t left;
    bool ok = true;
    void bytes(void* out, size_t n) {
        if (n > left) { ok = false; std::memset(out, 0, n); return; }
        std::memcpy(out, p, n);
        p += n;
        left -= n;
    }
    uint32_t u32() { uint32_t v; bytes(&v, 4); return v; }
    uint64_t u64() { uint64_t v; bytes(&v, 8); return v; }
};
constexpr uint32_t kStateMagic = 0x53345447;   // "GT4S"
constexpr uint32_t kStateVersion = 4;   // 1: VU1 memories and registers instead of the batch buffers; 2: no native state;
                                        // 3: the display as the GS's privileged registers
constexpr size_t kVramBytes = 4u << 20;
}  // namespace

std::vector<uint8_t> GraphicsStream::saveState(const uint8_t* vram) const {
    std::vector<uint8_t> out;
    out.reserve(kVramBytes + 64 * 1024);
    StateWriter w{out};
    w.u32(kStateMagic);
    w.u32(kStateVersion);
    w.bytes(vif1.program.data(), Vif1::kProgramBytes);
    w.bytes(vif1.buffers, sizeof vif1.buffers);
    w.bytes(&renderers.constants, sizeof renderers.constants);
    w.u32(vif1.continueAt);
    for (uint32_t v : {vif1.cl, vif1.wl, vif1.mode, vif1.mask, vif1.base, vif1.ofst, vif1.tops, vif1.itops, vif1.mark,
                       static_cast<uint32_t>(vif1.dbf), static_cast<uint32_t>(vif1.path3Masked)})
        w.u32(v);
    w.bytes(vif1.row, sizeof vif1.row);
    w.bytes(vif1.col, sizeof vif1.col);
    w.u32(displaySet ? 1 : 0);
    w.bytes(&displayState, sizeof displayState);
    w.bytes(gsRegsSet, sizeof gsRegsSet);
    w.bytes(gsRegs, sizeof gsRegs);
    w.u32(packedQ);
    for (const gfx::StateRecord& state : nativeState()) w.bytes(&state, sizeof state);
    w.bytes(vram, kVramBytes);
    return out;
}

void GraphicsStream::restoreState(const uint8_t* payload, size_t size) {
    StateReader r{payload, size};
    const uint32_t magic = r.u32(), version = r.u32();
    if (magic != kStateMagic || version < 1 || version > kStateVersion) {
        std::fprintf(stderr, "Replay: a saved state of an unknown version (ignored)\n");
        return;
    }
    r.bytes(vif1.program.data(), Vif1::kProgramBytes);
    vif1.programChanged();
    if (version == 1) {
        // Version 1 kept VU1 itself: its data memory holds the two batch buffers, its registers vf23..vf31 the
        // setup's constants, and the program counter where MSCNT goes on.
        std::vector<uint8_t> data(16384);
        uint32_t vf[32][4];
        uint8_t skip[16 + 32];   // ACC, vi
        r.bytes(data.data(), data.size());
        r.bytes(vf, sizeof vf);
        r.bytes(skip, sizeof skip);
        uint32_t registers[10];   // i, q, p, r, mac, status, clip, top, itop, pc
        for (uint32_t& v : registers) v = r.u32();
        vif1.continueAt = registers[9];
        renderers.constants = constantsFromRegisters(vf);
        for (uint32_t* v : {&vif1.cl, &vif1.wl, &vif1.mode, &vif1.mask, &vif1.base, &vif1.ofst, &vif1.tops, &vif1.itops, &vif1.mark}) *v = r.u32();
        for (uint32_t b = 0; b < 2; ++b) {
            const uint32_t start = (vif1.base + (b ? vif1.ofst : 0)) & 0x3FF;
            for (uint32_t q = 0; q < BatchBuffer::kQwords; ++q) std::memcpy(vif1.buffers[b].words[q], data.data() + ((start + q) & 0x3FF) * 16, 16);
        }
#if GT4_VU1_INTERPRETER
        if (reference) reference->tool.restore(data.data(), vf, vif1.program);
#endif
    } else {
        r.bytes(vif1.buffers, sizeof vif1.buffers);
        r.bytes(&renderers.constants, sizeof renderers.constants);
        vif1.continueAt = r.u32();
        for (uint32_t* v : {&vif1.cl, &vif1.wl, &vif1.mode, &vif1.mask, &vif1.base, &vif1.ofst, &vif1.tops, &vif1.itops, &vif1.mark}) *v = r.u32();
#if GT4_VU1_INTERPRETER
        if (reference) {   // the reference's memory model from the batch buffers, its registers from the constants
            std::vector<uint8_t> data(16384);
            for (uint32_t b = 0; b < 2; ++b) {
                const uint32_t start = (vif1.base + (b ? vif1.ofst : 0)) & 0x3FF;
                for (uint32_t q = 0; q < BatchBuffer::kQwords; ++q) std::memcpy(data.data() + ((start + q) & 0x3FF) * 16, vif1.buffers[b].words[q], 16);
            }
            uint32_t vf[32][4] = {};
            const Renderers3d::Constants& c = renderers.constants;
            const Vec4* regs[9] = {&c.guard, &c.clamp, &c.scale, &c.offset, nullptr, &c.matrix[0], &c.matrix[1], &c.matrix[2], &c.matrix[3]};
            for (int k = 0; k < 9; ++k)
                if (regs[k]) std::memcpy(vf[23 + k], regs[k], 16);
            reference->tool.restore(data.data(), vf, vif1.program);
        }
#endif
    }
    vif1.dbf = r.u32() != 0;
    vif1.path3Masked = r.u32() != 0;
    r.bytes(vif1.row, sizeof vif1.row);
    r.bytes(vif1.col, sizeof vif1.col);
    // the display: a record (version 4), or the privileged registers (PMODE .. BGCOLOR by offset / 16, and IMR)
    gfx::DisplayRecord display;
    bool hasDisplay = false;
    if (version >= 4) {
        hasDisplay = r.u32() != 0;
        r.bytes(&display, sizeof display);
    } else {
        const uint32_t privMask = r.u32();
        uint64_t priv[17];
        r.bytes(priv, sizeof priv);
        hasDisplay = (privMask & 0x4785) != 0;
        display = gfx::displayFromRegisters(priv[0x0], priv[0x2], priv[0x7], priv[0x8], priv[0x9], priv[0xA], priv[0xE]);
    }
    uint64_t regsSet[2], regs[kGsRegs];
    r.bytes(regsSet, sizeof regsSet);
    r.bytes(regs, sizeof regs);
    packedQ = r.u32();
    std::array<gfx::StateRecord, 2> native{};
    if (version >= 3)
        for (gfx::StateRecord& state : native) r.bytes(&state, sizeof state);
    if (!r.ok || r.left < kVramBytes) {
        std::fprintf(stderr, "Replay: a truncated saved state (ignored)\n");
        return;
    }
    if (hasDisplay) {
        displayState = display;
        displaySet = true;
        gs.display(display);
    }
    gs.writeVram(r.p);
    // The general registers, as one A+D packet; not the ones that start something (vertex kicks, a
    // transfer, the events).
    std::vector<uint64_t> packet{0, 0xE};
    for (uint32_t reg = 0; reg < kGsRegs; ++reg) {
        if (!(regsSet[reg >> 6] >> (reg & 63) & 1)) continue;
        if (reg == 0x04 || reg == 0x05 || reg == 0x0C || reg == 0x0D || reg == 0x53 || reg == 0x54 || reg >= 0x60) continue;
        bool isShared = false;
        uint32_t context = 0, group = 0;
        if (version >= 3 && gfx::groupOfRegister(reg, isShared, context, group)) continue;   // the native state below
        packet.push_back(regs[reg]);
        packet.push_back(reg);
    }
    const uint64_t count = (packet.size() - 2) / 2;
    packet[0] = count | 1ull << 15 | 1ull << 60;   // NLOOP, EOP, PACKED with one register (A+D)
    sendGif(2, reinterpret_cast<const uint8_t*>(packet.data()), packet.size() * 8);
    if (version >= 3)
        for (const gfx::StateRecord& state : native) applyNative(state);
}

// The native drawing state as two state records (every group, of context 1 and of context 2).
std::array<gfx::StateRecord, 2> GraphicsStream::nativeState() const {
    std::array<gfx::StateRecord, 2> out{};
    for (uint32_t k = 0; k < 2; ++k) {
        out[k].sharedGroups = k ? 0 : (1u << gfx::kSharedGroups) - 1;
        out[k].contextGroups = ((1u << gfx::kContextGroups) - 1) << (16 * k);
        out[k].shared = nativeShared;
        out[k].context = nativeContexts[k];
        out[k].palette = nativeContexts[k].texture;
    }
    return out;
}

void GraphicsStream::applyNative(const gfx::StateRecord& r) {
    gfx::applyState(nativeShared, nativeContexts, r, [](uint32_t, uint32_t) {});
    gs.state(r);
}

void GraphicsStream::releasePath3() {
    if (heldPath3.empty()) return;
    std::vector<uint8_t> data;
    data.swap(heldPath3);
    sendGif(3, data.data(), data.size());
}

void GraphicsStream::record(const Gt4LinkRecord& header, const uint8_t* payload) {
    ++counters.records;
    const uint32_t size = header.size;
    switch (header.type) {
    case GT4_REC_GIF: {
        if (size < 4) break;
        uint32_t path = 0;
        std::memcpy(&path, payload, 4);
        const size_t bytes = (size - 4) & ~size_t(15);
        if (path < 1 || path > 3) path = 3;
        ++counters.gifPackets[path];
        counters.gifBytes += bytes;
        if (path == 3 && vif1.path3Masked) {
            heldPath3.insert(heldPath3.end(), payload + 4, payload + 4 + bytes);
            ++counters.path3Held;
            break;
        }
        sendGif(path, payload + 4, bytes);
        break;
    }
    case GT4_REC_VIF1:
        counters.vif1Bytes += size;
        vif1.feed(payload, size);
        break;
    case GT4_REC_GFX:
        records(payload, size);
        break;
    case GT4_REC_GFX_DATA: {
        if (size < 8) break;
        uint32_t head[2];
        std::memcpy(head, payload, 8);
        RecordData d;
        d.source = head[0];
        d.bytes.assign(payload + 8, payload + 8 + std::min<size_t>(head[1], size - 8));
        recordData.push_back(std::move(d));
        break;
    }
    case GT4_REC_VIF0:
        ++counters.vif0Records;   // VU0 micro mode: not used by the renderer (logged in the statistics)
        break;
    case GT4_REC_DISPLAY: {
        if (size < sizeof(gfx::DisplayRecord)) break;
        std::memcpy(&displayState, payload, sizeof displayState);
        displaySet = true;
        ++counters.displays;
        gs.display(displayState);
        break;
    }
    case GT4_REC_STATE:
        restoreState(payload, size);
        break;
    case GT4_REC_VSYNC: {
        uint32_t field = 0, count = 0;
        if (size >= 4) std::memcpy(&field, payload, 4);
        if (size >= 8) std::memcpy(&count, payload + 4, 4);
        ++counters.vsyncs;
        lastVblank = count;
        if (!heldPath3.empty()) {   // still masked at the end of the frame: do not lose the data
            ++counters.path3ForcedAtVsync;
            releasePath3();
        }
        if (frameIndex == dumpFrame && dumpAtEnd) dumpVram("end");
        gs.vsync(field, count);
        ++frameIndex;
        packetIndex = 0;
        break;
    }
    case GT4_REC_GS_READ: {
        if (size < 8) break;
        uint32_t id = 0, qwc = 0;
        std::memcpy(&id, payload, 4);
        std::memcpy(&qwc, payload + 4, 4);
        ++counters.reads;
        scratch.assign(8 + size_t(qwc) * 16, 0);
        std::memcpy(scratch.data(), &id, 4);
        std::memcpy(scratch.data() + 4, &qwc, 4);
        gs.readTransfer(scratch.data() + 8, qwc);
        if (hostWriter) hostWriter(GT4_REC_GS_IMAGE, scratch.data(), static_cast<uint32_t>(scratch.size()));
        break;
    }
    case GT4_REC_LOG: {
        ++counters.logs;
        std::string text(reinterpret_cast<const char*>(payload), size);
        while (!text.empty() && (text.back() == '\n' || text.back() == '\0')) text.pop_back();
        std::printf("[game] %s\n", text.c_str());
        break;
    }
    case GT4_REC_AUDIO:
        ++counters.audio;
        if (audioWriter && size >= 8) {
            uint32_t id, rate;
            std::memcpy(&id, payload, 4);
            std::memcpy(&rate, payload + 4, 4);
            audioWriter(id, rate, reinterpret_cast<const int16_t*>(payload + 8), (size - 8) / 4);
        }
        break;
    case GT4_REC_SPU2_REGS:
    case GT4_REC_SPU2_RAM:
        ++counters.audio;   // the audio stream belongs to the audio back end
        break;
    default:
        ++counters.unknown;
        break;
    }
}

std::string GraphicsStream::describe() const {
    std::ostringstream out;
    if (std::getenv("GT4_VIF_CODES")) {   // (debugging) the VIF codes game code still wrote, by command
        out << "VIF codes in the stream:";
        for (int c = 0; c < 128; ++c)
            if (vif1.stats.streamCodes[c]) out << " " << std::hex << c << std::dec << ":" << vif1.stats.streamCodes[c];
        out << "; ";
    }
    const auto& v = vif1.stats;
    const auto& r = renderers.stats;
    out << counters.records << " records, " << counters.vsyncs << " vsyncs, GIF packets P1/P2/P3 "
        << counters.gifPackets[1] << "/" << counters.gifPackets[2] << "/" << counters.gifPackets[3] << " ("
        << (counters.gifBytes + 1023) / 1024 << " KiB PATH3), VIF1 " << counters.vif1Bytes / 1024 << " KiB: " << v.unpacks
        << " unpacks, " << v.microprograms << " 3D batches (" << r.batches << " drawn, " << counters.primitiveRuns << " primitive runs, "
        << counters.primitiveVertices << " vertices, " << r.clipped << " clipped triangles, " << r.skipped + counters.unknownPrograms
        << " without a renderer, " << r.unexpectedTags << " unexpected tags, " << r.countMismatch << " count mismatches, "
        << r.oversized << " oversized, " << v.outsideBatch << " qwords outside the batch buffers), " << v.directQwords
        << " DIRECT qwords, " << v.mpgInstructions << " MPG instructions, " << v.unknownCodes << " unknown codes; "
        << counters.gfxRecords << " native records (" << counters.uploads << " uploads, " << counters.uploadsWithoutData
         << " without data, " << counters.draws << " draws, " << counters.badRecords << " bad); " << counters.displays << " displays, " << counters.reads
        << " GS reads, " << counters.path3Held << " PATH3 records held by MSKPATH3 (" << counters.path3ForcedAtVsync
        << " released at VSYNC)";
    if (!vif1.lastWarning.empty()) out << "; last VIF warning: " << vif1.lastWarning;
    return out.str();
}

size_t splitRecords(const uint8_t* data, size_t size,
                    const std::function<void(const Gt4LinkRecord&, const uint8_t*)>& onRecord) {
    size_t offset = 0;
    while (offset + sizeof(Gt4LinkRecord) <= size) {
        Gt4LinkRecord header;
        std::memcpy(&header, data + offset, sizeof(header));
        const size_t span = recordSpan(header.size);
        if (offset + span > size) break;
        onRecord(header, data + offset + sizeof(header));
        offset += span;
    }
    return offset;
}

}  // namespace gt4::ps2
