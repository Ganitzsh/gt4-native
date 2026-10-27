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
    gs.gif(path, data, bytes);
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
        gs.privileged(reg, value);
        break;
    }
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
