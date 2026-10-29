// Game -> host graphics stream dispatch (see stream.h).
#include "stream.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace gt4::ps2 {

GraphicsStream::GraphicsStream(GsTarget& target) : gs(target), vif1(unit) {
    sink.owner = this;
    unit.setGifSink(&sink);
    vif1.setGifSink(&sink);
    vif1.onPath3Mask = [this](bool masked) { if (!masked) releasePath3(); };
    path1Stats = std::getenv("GT4_PATH1_STATS") != nullptr;
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

void GraphicsStream::sendGif(uint32_t path, const uint8_t* data, size_t bytes) {
    if (frameIndex == dumpFrame && std::find(dumpAt.begin(), dumpAt.end(), packetIndex) != dumpAt.end())
        dumpVram(std::to_string(packetIndex).c_str());
    if (frameIndex == logFrame) {
        uint64_t tag[2] = {};
        if (bytes >= 16) std::memcpy(tag, data, 16);
        std::printf("GIF %u: path %u, %zu bytes, tag %016llx %016llx, program %#x\n", packetIndex, path, bytes,
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
    ++packetIndex;
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
constexpr uint32_t kStateVersion = 1;
constexpr size_t kVramBytes = 4u << 20;
}  // namespace

std::vector<uint8_t> GraphicsStream::saveState(const uint8_t* vram) const {
    std::vector<uint8_t> out;
    out.reserve(kVramBytes + 64 * 1024);
    StateWriter w{out};
    w.u32(kStateMagic);
    w.u32(kStateVersion);
    w.bytes(unit.micro, sizeof unit.micro);
    w.bytes(unit.data, sizeof unit.data);
    w.bytes(unit.vf, sizeof unit.vf);
    w.bytes(&unit.acc, sizeof unit.acc);
    w.bytes(unit.vi, sizeof unit.vi);
    for (uint32_t v : {unit.i, unit.q, unit.p, unit.r, unit.mac, unit.status, unit.clip, unit.top, unit.itop, unit.pc()}) w.u32(v);
    for (uint32_t v : {vif1.cl, vif1.wl, vif1.mode, vif1.mask, vif1.base, vif1.ofst, vif1.tops, vif1.itops, vif1.mark,
                       static_cast<uint32_t>(vif1.dbf), static_cast<uint32_t>(vif1.path3Masked)})
        w.u32(v);
    w.bytes(vif1.row, sizeof vif1.row);
    w.bytes(vif1.col, sizeof vif1.col);
    w.u32(privSet);
    w.bytes(privRegs, sizeof privRegs);
    w.bytes(gsRegsSet, sizeof gsRegsSet);
    w.bytes(gsRegs, sizeof gsRegs);
    w.u32(packedQ);
    w.bytes(vram, kVramBytes);
    return out;
}

void GraphicsStream::restoreState(const uint8_t* payload, size_t size) {
    StateReader r{payload, size};
    if (r.u32() != kStateMagic || r.u32() != kStateVersion) {
        std::fprintf(stderr, "Replay: a saved state of an unknown version (ignored)\n");
        return;
    }
    r.bytes(unit.micro, sizeof unit.micro);
    r.bytes(unit.data, sizeof unit.data);
    r.bytes(unit.vf, sizeof unit.vf);
    r.bytes(&unit.acc, sizeof unit.acc);
    r.bytes(unit.vi, sizeof unit.vi);
    for (uint32_t* v : {&unit.i, &unit.q, &unit.p, &unit.r, &unit.mac, &unit.status, &unit.clip, &unit.top, &unit.itop}) *v = r.u32();
    unit.setPc(r.u32());
    for (uint32_t* v : {&vif1.cl, &vif1.wl, &vif1.mode, &vif1.mask, &vif1.base, &vif1.ofst, &vif1.tops, &vif1.itops, &vif1.mark}) *v = r.u32();
    vif1.dbf = r.u32() != 0;
    vif1.path3Masked = r.u32() != 0;
    r.bytes(vif1.row, sizeof vif1.row);
    r.bytes(vif1.col, sizeof vif1.col);
    const uint32_t privMask = r.u32();
    uint64_t priv[17];
    r.bytes(priv, sizeof priv);
    uint64_t regsSet[2], regs[kGsRegs];
    r.bytes(regsSet, sizeof regsSet);
    r.bytes(regs, sizeof regs);
    packedQ = r.u32();
    if (!r.ok || r.left < kVramBytes) {
        std::fprintf(stderr, "Replay: a truncated saved state (ignored)\n");
        return;
    }
    for (uint32_t k = 0; k < 17; ++k)
        if (privMask >> k & 1) {
            const uint32_t offset = k < 16 ? k * 0x10 : 0x1010;
            privRegs[k] = priv[k];
            gs.privileged(offset, priv[k]);
        }
    privSet = privMask;
    gs.writeVram(r.p);
    // The general registers, as one A+D packet; not the ones that start something (vertex kicks, a
    // transfer, the events).
    std::vector<uint64_t> packet{0, 0xE};
    for (uint32_t reg = 0; reg < kGsRegs; ++reg) {
        if (!(regsSet[reg >> 6] >> (reg & 63) & 1)) continue;
        if (reg == 0x04 || reg == 0x05 || reg == 0x0C || reg == 0x0D || reg == 0x53 || reg == 0x54 || reg >= 0x60) continue;
        packet.push_back(regs[reg]);
        packet.push_back(reg);
    }
    const uint64_t count = (packet.size() - 2) / 2;
    packet[0] = count | 1ull << 15 | 1ull << 60;   // NLOOP, EOP, PACKED with one register (A+D)
    sendGif(2, reinterpret_cast<const uint8_t*>(packet.data()), packet.size() * 8);
}

// PACKED RGBAQ (register 1) values of a PATH1 packet, counted by colour (debugging aid).
void GraphicsStream::countColours(const uint8_t* data, size_t bytes) {
    size_t q = 0;
    const size_t n = bytes / 16;
    while (q < n) {
        uint64_t lo, hi;
        std::memcpy(&lo, data + q * 16, 8);
        std::memcpy(&hi, data + q * 16 + 8, 8);
        ++q;
        const uint32_t nloop = lo & 0x7FFF, flg = (lo >> 58) & 3;
        uint32_t nreg = (lo >> 60) & 15;
        if (!nreg) nreg = 16;
        if (flg != 0) {
            q += flg == 1 ? (nloop * nreg + 1) / 2 : nloop;
            continue;
        }
        for (uint32_t l = 0; l < nloop && q < n; ++l)
            for (uint32_t r = 0; r < nreg && q < n; ++r, ++q) {
                const uint32_t reg = (hi >> (4 * r)) & 15;
                uint32_t w[4];
                std::memcpy(w, data + q * 16, 16);
                if (reg == 0xE && ((w[2] & 0xFF) == 0x4C || (w[2] & 0xFF) == 0x4D)) {  // A+D FRAME: fbp, psm
                    const uint32_t key = 0x40000000u | (w[0] & 0x1FF) | ((w[0] >> 24) & 0x3F) << 12;
                    auto it = std::find_if(texCounts.begin(), texCounts.end(), [key](const auto& e) { return e.first == key; });
                    if (it != texCounts.end()) ++it->second;
                    else texCounts.push_back({key, 1});
                    continue;
                }
                if (reg == 0xE && ((w[2] & 0xFF) == 6 || (w[2] & 0xFF) == 7)) {  // A+D TEX0: psm, cpsm, tfx, tcc
                    const uint64_t t = w[0] | static_cast<uint64_t>(w[1]) << 32;
                    const uint32_t key = 0x80000000u | static_cast<uint32_t>((t >> 20) & 0x3F) | static_cast<uint32_t>((t >> 51) & 0xF) << 8 |
                                         static_cast<uint32_t>((t >> 35) & 3) << 12 | static_cast<uint32_t>((t >> 34) & 1) << 14;
                    auto it = std::find_if(texCounts.begin(), texCounts.end(), [key](const auto& e) { return e.first == key; });
                    if (it != texCounts.end()) ++it->second;
                    else texCounts.push_back({key, 1});
                    continue;
                }
                if (reg != 1) continue;
                const uint32_t c = (w[0] & 0xFF) | (w[1] & 0xFF) << 8 | (w[2] & 0xFF) << 16 | (w[3] & 0xFF) << 24;
                {
                    const uint32_t prog = vif1.lastProgram;
                    auto pc = std::find_if(programCounts.begin(), programCounts.end(), [prog](const auto& e) { return e.first == prog; });
                    if (pc == programCounts.end()) { programCounts.push_back({prog, {0, 0}}); pc = programCounts.end() - 1; }
                    if ((c & 0xFFFFFF) == 0) ++pc->second.first;
                    ++pc->second.second;
                }
                auto it = std::find_if(colourCounts.begin(), colourCounts.end(), [c](const auto& e) { return e.first == c; });
                if (it != colourCounts.end()) ++it->second;
                else if (colourCounts.size() < 4096) colourCounts.push_back({c, 1});
            }
    }
}

void GraphicsStream::reportColours(uint32_t frame) {
    if (colourCounts.empty()) return;
    std::sort(colourCounts.begin(), colourCounts.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    std::printf("PATH1 colours, frame %u:", frame);
    for (size_t i = 0; i < colourCounts.size() && i < 12; ++i) {
        const uint32_t c = colourCounts[i].first;
        std::printf(" (%u,%u,%u,%u)x%u", c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF, c >> 24, colourCounts[i].second);
    }
    std::printf("\n  TEX0 (psm cpsm tfx tcc):");
    for (const auto& [k, count] : texCounts) {
        if (k & 0x80000000u) std::printf(" (%#x %#x %u %u)x%u", k & 0x3F, (k >> 8) & 0xF, (k >> 12) & 3, (k >> 14) & 1, count);
        else std::printf(" FRAME(fbp %#x psm %#x)x%u", k & 0x1FF, (k >> 12) & 0x3F, count);
    }
    std::printf("\n  programs (black/all vertices):");
    for (const auto& [prog, c] : programCounts) std::printf(" %#x %u/%u", prog, c.first, c.second);
    std::printf("\n");
    programCounts.clear();
    colourCounts.clear();
    texCounts.clear();
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
    case GT4_REC_VIF0:
        ++counters.vif0Records;   // VU0 micro mode: not used by the renderer (logged in the statistics)
        break;
    case GT4_REC_GS_PRIV: {
        if (size < 16) break;
        uint32_t reg = 0;
        uint64_t value = 0;
        std::memcpy(&reg, payload, 4);
        std::memcpy(&value, payload + 8, 8);
        ++counters.privWrites;
        if (reg < 0x100 || reg == 0x1010) {
            const uint32_t slot = reg < 0x100 ? reg >> 4 : 16;
            privRegs[slot] = value;
            privSet |= 1u << slot;
        }
        gs.privileged(reg, value);
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
        if (path1Stats) reportColours(static_cast<uint32_t>(counters.vsyncs));
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
    const auto& v = vif1.stats;
    out << counters.records << " records, " << counters.vsyncs << " vsyncs, GIF packets P1/P2/P3 "
        << counters.gifPackets[1] << "/" << counters.gifPackets[2] << "/" << counters.gifPackets[3] << " ("
        << (counters.gifBytes + 1023) / 1024 << " KiB PATH3), VIF1 " << counters.vif1Bytes / 1024 << " KiB: " << v.unpacks
        << " unpacks, " << v.microprograms << " VU1 programs (" << v.vuCycles << " cycles, " << v.runaways
        << " runaways), " << v.directQwords << " DIRECT qwords, " << v.mpgInstructions << " MPG instructions, "
        << v.unknownCodes << " unknown codes; " << counters.privWrites << " privileged writes, " << counters.reads
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
