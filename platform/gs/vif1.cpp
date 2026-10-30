// VIF1 command decoding (see vif1.h).
#include "vif1.h"

#include <algorithm>
#include <cstring>

namespace gt4::ps2 {

namespace {

constexpr uint32_t cmdOf(uint32_t code) { return (code >> 24) & 0x7F; }

// Bytes of one UNPACK element: (32 >> vl) * (vn + 1) / 8; V4-5 is 2.
uint32_t elementBytes(uint32_t cmd) {
    const uint32_t vn = (cmd >> 2) & 3, vl = cmd & 3;
    if (vn == 3 && vl == 3) return 2;
    return ((32u >> vl) * (vn + 1)) / 8;
}

// FNV-1a of GT4's micro program image (NTSC-U v1.01, 16 KiB as uploaded at boot).
constexpr uint64_t kGt4Program = 0x8B6E3B5747FAFFF5ull;

}  // namespace

Vif1::Vif1() = default;

void Vif1::reset() {
    cl = wl = 1;
    mode = mask = 0;
    std::memset(row, 0, sizeof(row));
    std::memset(col, 0, sizeof(col));
    base = ofst = tops = itops = 0;
    dbf = false;
    mark = 0;
    path3Masked = false;
    pending.clear();
    pos = 0;
    stats = {};
    for (BatchBuffer& b : buffers) b = {};
    continueAt = 0;
}

bool Vif1::gt4Program() {
    if (!programKnown) {
        uint64_t h = 0xCBF29CE484222325ull;
        for (const uint8_t byte : program) h = (h ^ byte) * 0x100000001B3ull;
        programIsGt4 = h == kGt4Program;
        programKnown = true;
    }
    return programIsGt4;
}

void Vif1::feed(const uint8_t* bytes, size_t size) {
    const size_t words = size / 4;
    const size_t old = pending.size();
    pending.resize(old + words);
    std::memcpy(pending.data() + old, bytes, words * 4);
    while (pos < pending.size() && step()) {}
    // Drop what was consumed (keep an incomplete command for the next call).
    if (pos) {
        pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(pos));
        pos = 0;
    }
}

uint32_t Vif1::unpackWords(uint32_t code, uint32_t cycleCl, uint32_t cycleWl) {
    const uint32_t cmd = cmdOf(code);
    uint32_t num = (code >> 16) & 0xFF;
    if (!num) num = 256;
    const uint32_t writeLength = cycleWl ? cycleWl : 256;
    uint32_t elements = num;
    if (cycleCl < writeLength) elements = cycleCl * (num / writeLength) + std::min(num % writeLength, cycleCl);
    return (elements * elementBytes(cmd) + 3) / 4;
}

bool Vif1::step() {
    const size_t available = pending.size() - pos;
    const uint32_t code = pending[pos];
    const uint32_t cmd = cmdOf(code);
    const uint32_t imm = code & 0xFFFF;
    const uint32_t* next = pending.data() + pos + 1;
    auto need = [&](size_t words) { return available >= 1 + words; };
    size_t consumed = 1;
    ++stats.codes;
    if (cmd >= 0x60) {   // UNPACK
        const uint32_t words = unpackWords(code, cl, wl);
        if (!need(words)) { --stats.codes; return false; }
        unpack(code, next);
        consumed += words;
        ++stats.unpacks;
    } else {
        switch (cmd) {
        case 0x00: break;                                                    // NOP
        case 0x01: cl = imm & 0xFF; wl = (imm >> 8) & 0xFF; break;           // STCYCL
        case 0x02: ofst = imm & 0x3FF; dbf = false; tops = base; break;      // OFFSET
        case 0x03: base = imm & 0x3FF; break;                                // BASE
        case 0x04: itops = imm & 0x3FF; break;                               // ITOP
        case 0x05: mode = imm & 3; break;                                    // STMOD
        case 0x06: {                                                         // MSKPATH3
            const bool masked = (imm >> 15) & 1;
            const bool changed = masked != path3Masked;
            path3Masked = masked;
            if (changed && onPath3Mask) onPath3Mask(masked);
            break;
        }
        case 0x07: mark = imm; break;                                        // MARK
        case 0x10: case 0x11: case 0x13: break;                              // FLUSHE FLUSH FLUSHA
        case 0x14: case 0x15: start(imm); break;                             // MSCAL MSCALF
        case 0x17: start(continueAt); break;                                 // MSCNT
        case 0x20:                                                           // STMASK
            if (!need(1)) { --stats.codes; return false; }
            mask = next[0];
            consumed += 1;
            break;
        case 0x30: case 0x31:                                                // STROW STCOL
            if (!need(4)) { --stats.codes; return false; }
            std::memcpy(cmd == 0x30 ? row : col, next, 16);
            consumed += 4;
            break;
        case 0x4A: {                                                         // MPG
            uint32_t num = (code >> 16) & 0xFF;
            if (!num) num = 256;
            if (!need(num * 2)) { --stats.codes; return false; }
            for (uint32_t k = 0; k < num; ++k)
                std::memcpy(program.data() + ((imm + k) & 0x7FF) * 8, next + 2 * k, 8);
#if GT4_VU1_INTERPRETER
            if (observer) observer->mpg(imm, next, num);
#endif
            programKnown = false;
            stats.mpgInstructions += num;
            consumed += num * 2;
            break;
        }
        case 0x50: case 0x51: {                                              // DIRECT DIRECTHL
            const uint32_t qwc = imm ? imm : 65536;
            if (!need(size_t(qwc) * 4)) { --stats.codes; return false; }
            if (gifSink) gifSink->gif(2, reinterpret_cast<const uint8_t*>(next), size_t(qwc) * 16);
            stats.directQwords += qwc;
            consumed += size_t(qwc) * 4;
            break;
        }
        case 0x08: {                                                         // native records (include/gt4/gfx.h)
            const uint32_t qwc = imm;
            if (!need(size_t(qwc) * 4)) { --stats.codes; return false; }
            if (onRecords) onRecords(reinterpret_cast<const uint8_t*>(next), size_t(qwc) * 16);
            stats.recordQwords += qwc;
            consumed += size_t(qwc) * 4;
            break;
        }
        default:
            ++stats.unknownCodes;
            lastWarning = "unknown VIF1 code " + std::to_string(cmd);
            break;
        }
    }
    pos += consumed;
    return true;
}

// MSCAL: the batch in the current buffer is complete. The next batch goes to the other buffer (the swap
// the VIF makes at each start: TOPS alternates between BASE and BASE + OFFSET).
void Vif1::start(uint32_t address) {
    Batch3d batch;
    batch.entry = address;
    batch.itop = itops;
    batch.top = tops & 0x3FF;
    batch.buffer = &buffers[dbf ? 1 : 0];
    if (dbf) {
        tops = base;
        dbf = false;
    } else {
        tops = base + ofst;
        dbf = true;
    }
    lastProgram = address * 8;
    ++stats.microprograms;
    if (onBatch) onBatch(batch);
}

void Vif1::unpack(uint32_t code, const uint32_t* source) {
    const uint32_t cmd = cmdOf(code);
    const uint32_t vn = (cmd >> 2) & 3, vl = cmd & 3;
    const bool masked = (cmd >> 4) & 1;
    const bool usn = (code >> 14) & 1;
    const bool relative = (code >> 15) & 1;   // FLG: from the start of the current batch buffer
    uint32_t num = (code >> 16) & 0xFF;
    if (!num) num = 256;
    uint32_t address = code & 0x3FF;
    if (!relative) ++stats.absoluteUnpacks;
    const uint32_t writeLength = wl ? wl : 256;
    const bool fill = cl < writeLength;
    const uint32_t bytesPerElement = elementBytes(cmd);
    const uint32_t dataWords = unpackWords(code, cl, wl);
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(source);
    const size_t dataBytes = size_t(dataWords) * 4;
    const bool v45 = vn == 3 && vl == 3;
    const uint32_t componentBytes = v45 ? 2 : (4u >> vl);

    auto component = [&](size_t offset) -> uint32_t {
        if (offset + componentBytes > dataBytes) return 0;   // past the end of the data (V3 W): zero
        switch (componentBytes) {
        case 4: { uint32_t v; std::memcpy(&v, bytes + offset, 4); return v; }
        case 2: {
            uint16_t v; std::memcpy(&v, bytes + offset, 2);
            return usn ? v : static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(v)));
        }
        default: {
            const uint8_t v = bytes[offset];
            return usn ? v : static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(v)));
        }
        }
    };
    // Where a qword goes: relative addresses are in the current buffer; absolute ones (GT4 has none) in the
    // buffer whose VU address range holds them.
    const uint32_t current = dbf ? 1 : 0;
    const uint32_t bufferStart[2] = {base & 0x3FF, (base + ofst) & 0x3FF};
    auto destinationOf = [&](uint32_t a, uint32_t& vuAddress) -> uint32_t* {
        if (relative) {
            vuAddress = (bufferStart[current] + a) & 0x3FF;
            if (a < BatchBuffer::kQwords) return buffers[current].words[a];
            return nullptr;
        }
        vuAddress = a & 0x3FF;
        for (uint32_t b : {current, current ^ 1u}) {
            const uint32_t r = (vuAddress - bufferStart[b]) & 0x3FF;
            if (r < BatchBuffer::kQwords) return buffers[b].words[r];
        }
        return nullptr;
    };

    size_t dataOffset = 0;
    uint32_t cycle = 0;
    for (uint32_t n = 0; n < num; ++n) {
        uint32_t value[4];
        if (v45) {
            uint16_t v = 0;
            if (dataOffset + 2 <= dataBytes) std::memcpy(&v, bytes + dataOffset, 2);
            value[0] = (v & 0x001Fu) << 3;
            value[1] = (v & 0x03E0u) >> 2;
            value[2] = (v & 0x7C00u) >> 7;
            value[3] = (v & 0x8000u) >> 8;
        } else if (vn == 0) {
            value[0] = value[1] = value[2] = value[3] = component(dataOffset);
        } else if (vn == 1) {
            value[0] = value[2] = component(dataOffset);
            value[1] = value[3] = component(dataOffset + componentBytes);
        } else {   // V3 and V4 both read four components (V3's W is the next element's first)
            for (uint32_t c = 0; c < 4; ++c) value[c] = component(dataOffset + c * componentBytes);
        }
        uint32_t vuAddress = 0;
        uint32_t* destination = destinationOf(address & 0x3FF, vuAddress);
        uint32_t scratch[4] = {};
        if (!destination) {
            ++stats.outsideBatch;
            destination = scratch;
        }
        const uint32_t maskCycle = std::min(cycle, 3u);
        unsigned lanes = 0;
        for (uint32_t f = 0; f < 4; ++f) {
            const uint32_t m = masked ? (mask >> (maskCycle * 8 + f * 2)) & 3 : 0;
            switch (m) {
            case 0:
                switch (v45 ? 0 : mode) {
                case 1: destination[f] = value[f] + row[f]; break;
                case 2: row[f] += value[f]; destination[f] = row[f]; break;
                case 3: row[f] = value[f]; destination[f] = value[f]; break;
                default: destination[f] = value[f]; break;
                }
                lanes |= 1u << f;
                break;
            case 1: destination[f] = row[f]; lanes |= 1u << f; break;
            case 2: destination[f] = col[maskCycle]; lanes |= 1u << f; break;
            default: break;   // write protected
            }
        }
#if GT4_VU1_INTERPRETER
        if (observer) observer->unpacked(vuAddress, destination, lanes);
#endif
        (void)lanes;
        ++address;
        ++cycle;
        if (fill) {
            if (cycle <= cl) dataOffset += bytesPerElement;
            else if (cycle == writeLength) cycle = 0;
        } else {
            dataOffset += bytesPerElement;
            if (cycle >= writeLength) {
                address += cl - writeLength;
                cycle = 0;
            }
        }
    }
}

}  // namespace gt4::ps2
