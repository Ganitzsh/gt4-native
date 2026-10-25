// Game -> host graphics stream dispatch (see stream.h).
#include "stream.h"

#include <cstdio>
#include <cstring>
#include <sstream>

namespace gt4::ps2 {

GraphicsStream::GraphicsStream(GsTarget& target) : gs(target), vif1(unit) {
    sink.owner = this;
    unit.setGifSink(&sink);
    vif1.setGifSink(&sink);
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
        gs.gif(path, payload + 4, bytes);
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
        gs.vsync(field, count);
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
        << " GS reads";
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
