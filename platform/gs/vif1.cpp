// VIF1 command decoding (see vif1.h).
#include "vif1.h"

#include "gt4/unpack.h"

#include <algorithm>
#include <cstring>

namespace gt4::ps2 {

namespace {

constexpr uint32_t cmdOf(uint32_t code) { return (code >> 24) & 0x7F; }

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
    uint32_t num = (code >> 16) & 0xFF;
    if (!num) num = 256;
    return gfx::unpackWords(cmdOf(code), num, cycleCl, cycleWl);
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

void Vif1::batchFrame() {
    base = 0;
    ofst = 435;
    tops = base;
    dbf = false;
    cl = wl = 4;
    mode = 0;
    mask = 0xC0C0C0C0u;
}

void Vif1::batchWrite(uint32_t qword, const uint32_t* words, uint32_t count, uint32_t lanes) {
    BatchBuffer& b = buffers[dbf ? 1 : 0];
    for (uint32_t k = 0; k < count; ++k) {
        if (qword + k >= BatchBuffer::kQwords) {
            ++stats.outsideBatch;
            continue;
        }
        for (uint32_t f = 0; f < 4; ++f)
            if ((lanes >> f) & 1) b.words[qword + k][f] = words[4 * k + f];
    }
}

void Vif1::batchModel(const uint8_t* bytes, size_t size, uint32_t modelMask) {
    mask = modelMask;
    // Decoded on its own: the record comes from inside the stream being decoded (its words, and `pos`, stay as
    // they are; swapping keeps the outer words where the caller's pointers expect them).
    std::vector<uint32_t> outer;
    outer.swap(pending);
    const size_t outerPos = pos;
    pos = 0;
    feed(bytes, size);
    if (!pending.empty()) {
        lastWarning = "a model packet ends inside a command";
        pending.clear();
    }
    pending.swap(outer);
    pos = outerPos;
}

void Vif1::batchRun(uint32_t entry, uint32_t itop) {
    if (itop != 0xFFFFFFFFu) itops = itop & 0x3FF;
    start(entry);
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
    const bool usn = (code >> 14) & 1;
    const bool relative = (code >> 15) & 1;   // FLG: from the start of the current batch buffer
    uint32_t num = (code >> 16) & 0xFF;
    if (!num) num = 256;
    const uint32_t address = code & 0x3FF;
    if (!relative) ++stats.absoluteUnpacks;
    const size_t dataBytes = size_t(gfx::unpackWords(cmd, num, cl, wl)) * 4;
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
    gfx::unpackExpand(cmd, num, usn, cl, wl, mask, mode, row, col, reinterpret_cast<const uint8_t*>(source), dataBytes,
                      [&](uint32_t offset, const uint32_t value[4], unsigned lanes) {
                          uint32_t vuAddress = 0;
                          uint32_t* destination = destinationOf((address + offset) & 0x3FF, vuAddress);
                          if (!destination) {
                              ++stats.outsideBatch;
                              return;
                          }
                          for (uint32_t f = 0; f < 4; ++f)
                              if ((lanes >> f) & 1) destination[f] = value[f];
#if GT4_VU1_INTERPRETER
                          if (observer) observer->unpacked(vuAddress, destination, lanes);
#endif
                      });
}

}  // namespace gt4::ps2
