// VIF1 command decoding (see vif1.h).
#include "vif1.h"

#include "gt4/unpack.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
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
        // GT4's, or none uploaded (the native game sends none: its renderers are the host's)
        programIsGt4 = h == kGt4Program || std::all_of(program.begin(), program.end(), [](uint8_t b) { return b == 0; });
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
    if (!inModel) ++stats.streamCodes[cmd];
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
            cacheable = false;
            const bool masked = (imm >> 15) & 1;
            const bool changed = masked != path3Masked;
            path3Masked = masked;
            if (changed && onPath3Mask) onPath3Mask(masked);
            break;
        }
        case 0x07: mark = imm; break;                                        // MARK
        case 0x10: case 0x11: case 0x13: break;                              // FLUSHE FLUSH FLUSHA
        case 0x14: case 0x15: cacheable = false; start(imm); break;          // MSCAL MSCALF
        case 0x17: cacheable = false; start(continueAt); break;              // MSCNT
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
            cacheable = false;
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
            cacheable = false;
            const uint32_t qwc = imm ? imm : 65536;
            if (!need(size_t(qwc) * 4)) { --stats.codes; return false; }
            if (gifSink) gifSink->gif(2, reinterpret_cast<const uint8_t*>(next), size_t(qwc) * 16);
            stats.directQwords += qwc;
            consumed += size_t(qwc) * 4;
            break;
        }
        case 0x08: {                                                         // native records (include/gt4/gfx.h)
            cacheable = false;
            const uint32_t qwc = imm;
            if (!need(size_t(qwc) * 4)) { --stats.codes; return false; }
            if (onRecords) onRecords(reinterpret_cast<const uint8_t*>(next), size_t(qwc) * 16);
            stats.recordQwords += qwc;
            consumed += size_t(qwc) * 4;
            break;
        }
        default: {
            ++stats.unknownCodes;
            lastWarning = "unknown VIF1 code " + std::to_string(cmd);
            static int logged = 0;
            if (logged++ < 6) std::fprintf(stderr, "vif1: unknown code %08X (%s, %zu words left)\n", code, inModel ? "model packet" : "stream", available);
            break;
        }
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
        if (qword + k < 192) ++b.vertexWrites;
    }
}

void Vif1::batchModel(const uint8_t* bytes, size_t size, uint32_t modelMask, uint64_t contentHash) {
    mask = modelMask;
    // Decoded on its own: the record comes from inside the stream being decoded (its words, and `pos`, stay as
    // they are; swapping keeps the outer words where the caller's pointers expect them).
    std::vector<uint32_t> outer;
    outer.swap(pending);
    const size_t outerPos = pos;
    pos = 0;
    inModel = true;
    if (std::getenv("GT4_MODEL_LOG")) {   // (debugging) the first codes of model packets, the cycle they start with
        static int left = std::atoi(std::getenv("GT4_MODEL_LOG"));
        if (left-- > 0) {
            uint32_t w[4] = {};
            std::memcpy(w, bytes, std::min<size_t>(size, 16));
            std::fprintf(stderr, "model: %zu bytes, cycle %u/%u, mask %08X, first words %08X %08X %08X %08X\n", size, cl, wl, modelMask, w[0], w[1],
                         w[2], w[3]);
        }
    }
    // Decoded once per (bytes, VIF state); after that its effect is replayed (GT4_VIF_MODELS=0: decoded every time).
    static const bool memo = [] { const char* e = std::getenv("GT4_VIF_MODELS"); return !(e && *e == '0'); }();
    uint64_t key = contentHash ? contentHash ^ size : 0xCBF29CE484222325ull ^ size;
    if (memo) {
        if (!contentHash) {
            const uint8_t* b = bytes;
            size_t i = 0;
            for (; i + 8 <= size; i += 8) {
                uint64_t w;
                std::memcpy(&w, b + i, 8);
                key = (key ^ w) * 0x9E3779B97F4A7C15ull;
                key ^= key >> 29;
            }
            for (; i < size; ++i) key = (key ^ b[i]) * 0x100000001B3ull;
        }
        const uint32_t state[] = {cl, wl, mask, mode, row[0], row[1], row[2], row[3], col[0], col[1], col[2], col[3],
                                  base, ofst, tops, itops, dbf ? 1u : 0u};
        for (const uint32_t w : state) key = (key ^ w) * 0x100000001B3ull;
        auto known = modelEffects.find(key);
        if (known != modelEffects.end()) {
            const ModelEffect& e = known->second;
            for (const ModelEffect::Write& w : e.writes) {
                uint32_t* q = buffers[w.buffer].words[w.qword];
                for (uint32_t f = 0; f < 4; ++f)
                    if ((w.lanes >> f) & 1) q[f] = w.value[f];
                if (w.qword < 192) ++buffers[w.buffer].vertexWrites;
            }
            cl = e.cl, wl = e.wl, mask = e.mask, mode = e.mode, base = e.base, ofst = e.ofst, tops = e.tops, itops = e.itops, mark = e.mark;
            dbf = e.dbf;
            std::memcpy(row, e.row, sizeof row);
            std::memcpy(col, e.col, sizeof col);
            stats.unpacks += e.unpacks;
            stats.codes += e.codes;
            ++modelStats.replayed;
            inModel = false;
            pending.swap(outer);
            pos = outerPos;
            return;
        }
    }
    ModelEffect effect;
    const uint64_t unpacksBefore = stats.unpacks, codesBefore = stats.codes;
    recording = memo ? &effect : nullptr;
    cacheable = true;
    feed(bytes, size);
    recording = nullptr;
    inModel = false;
    if (!pending.empty()) {
        lastWarning = "a model packet ends inside a command";
        pending.clear();
        cacheable = false;
    }
    ++modelStats.decoded;
    if (memo && cacheable) {
        effect.cl = cl, effect.wl = wl, effect.mask = mask, effect.mode = mode, effect.base = base, effect.ofst = ofst, effect.tops = tops;
        effect.itops = itops, effect.mark = mark, effect.dbf = dbf;
        std::memcpy(effect.row, row, sizeof row);
        std::memcpy(effect.col, col, sizeof col);
        effect.unpacks = stats.unpacks - unpacksBefore;
        effect.codes = stats.codes - codesBefore;
        if (modelEffects.size() > 32768) modelEffects.clear();   // (a bound: models of courses left long ago)
        modelEffects.emplace(key, std::move(effect));
    } else if (memo) {
        ++modelStats.uncacheable;
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
    uint32_t written = 0, writtenQword = 0;   // (the buffer and qword destinationOf found, for a recording)
    auto destinationOf = [&](uint32_t a, uint32_t& vuAddress) -> uint32_t* {
        if (relative) {
            vuAddress = (bufferStart[current] + a) & 0x3FF;
            if (a < 192) ++buffers[current].vertexWrites;
            written = current, writtenQword = a;
            if (a < BatchBuffer::kQwords) return buffers[current].words[a];
            return nullptr;
        }
        vuAddress = a & 0x3FF;
        for (uint32_t b : {current, current ^ 1u}) {
            const uint32_t r = (vuAddress - bufferStart[b]) & 0x3FF;
            if (r < 192) ++buffers[b].vertexWrites;
            written = b, writtenQword = r;
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
                          if (recording)
                              recording->writes.push_back({static_cast<uint16_t>(written), static_cast<uint16_t>(writtenQword), lanes,
                                                           {destination[0], destination[1], destination[2], destination[3]}});
#if GT4_VU1_INTERPRETER
                          if (observer) observer->unpacked(vuAddress, destination, lanes);
#endif
                      });
}

}  // namespace gt4::ps2
