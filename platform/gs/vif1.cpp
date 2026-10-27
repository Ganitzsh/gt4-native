// VIF1 command decoding (development scaffold; see vif1.h).
#include "vif1.h"

#include "vu1_inventory.h"
#include "vu1_native.h"
#include "vu1_ref.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>

namespace gt4::ps2 {

namespace {

// GT4_VU1_DIFF=1: every microprogram also runs in the reference interpreter (vu1_ref.cpp) from the same
// state; differences in the PATH1 packets, data memory and registers are reported per program
// (the first ones in detail, a table at exit).
struct VuDiff final : GifSink {
    GifSink* next = nullptr;
    std::vector<uint8_t> kicks;
    void gif(uint32_t path, const uint8_t* data, size_t bytes) override {
        if (path == 1) kicks.insert(kicks.end(), data, data + bytes);
        if (next) next->gif(path, data, bytes);
    }
    std::unique_ptr<Vu1Ref> ref = std::make_unique<Vu1Ref>();
    struct Count { uint64_t runs = 0, kick = 0, mem = 0, regs = 0; };
    std::map<uint32_t, Count> programs;
    ~VuDiff() override {
        std::fprintf(stderr, "[vu1 diff] program: runs, PATH1 differs, data differs, registers differ\n");
        for (const auto& [address, c] : programs)
            std::fprintf(stderr, "[vu1 diff] %04X: %llu %llu %llu %llu\n", address * 8, (unsigned long long)c.runs,
                         (unsigned long long)c.kick, (unsigned long long)c.mem, (unsigned long long)c.regs);
    }
};

VuDiff* vuDiff() {
    static VuDiff* diff = std::getenv("GT4_VU1_DIFF") ? new VuDiff : nullptr;
    static struct Free { ~Free() { delete vuDiff(); } } freeAtExit;
    return diff;
}

bool wordsDiffer(const uint8_t* a, const uint8_t* b, size_t bytes);

void reportQwords(const char* what, uint32_t address, const uint8_t* ours, const uint8_t* theirs, size_t bytes) {
    int shown = 0;
    for (size_t q = 0; q * 16 < bytes && shown < 4; ++q) {
        if (!wordsDiffer(ours + q * 16, theirs + q * 16, 16)) continue;
        uint32_t a[4], b[4];
        std::memcpy(a, ours + q * 16, 16);
        std::memcpy(b, theirs + q * 16, 16);
        std::fprintf(stderr, "[vu1 diff] %04X %s qword %zu: ours %08X %08X %08X %08X, reference %08X %08X %08X %08X\n",
                     address * 8, what, q, a[0], a[1], a[2], a[3], b[0], b[1], b[2], b[3]);
        ++shown;
    }
}

// Words that differ by more than rounding: floats beyond a relative 1e-4, other words exactly
// (integers within 16, the last bits of a fixed-point coordinate).
bool wordsDiffer(const uint8_t* a, const uint8_t* b, size_t bytes) {
    for (size_t k = 0; k + 4 <= bytes; k += 4) {
        uint32_t x, y;
        std::memcpy(&x, a + k, 4);
        std::memcpy(&y, b + k, 4);
        if (x == y) continue;
        const uint32_t ex = (x >> 23) & 0xFF, ey = (y >> 23) & 0xFF;
        if (ex > 0x40 && ex < 0xC0 && ey > 0x40 && ey < 0xC0) {
            float fx, fy;
            std::memcpy(&fx, &x, 4);
            std::memcpy(&fy, &y, 4);
            if (std::fabs(fx - fy) <= 1e-4f * std::max(std::fabs(fx), std::fabs(fy))) continue;
            return true;
        }
        if (static_cast<uint32_t>(std::abs(static_cast<int32_t>(x - y))) <= 16) continue;
        return true;
    }
    return false;
}

void runWithDiff(VuDiff& d, Vu1& vu, uint32_t address, uint64_t& cycles) {
    // GT4_VU1_TRACE=ADDR (byte address): trace the first run of that program in both interpreters
    // (vu1_trace_ours.txt, vu1_trace_ref.txt).
    static const long traced = std::getenv("GT4_VU1_TRACE") ? std::strtol(std::getenv("GT4_VU1_TRACE"), nullptr, 16) : -1;
    static bool traceDone = false;
    const bool tracing = !traceDone && traced == static_cast<long>(address * 8);
    if (tracing) {
        traceDone = true;
        d.ref->trace = std::fopen("vu1_trace_ref.txt", "w");
        vu.trace = std::fopen("vu1_trace_ours.txt", "w");
    }
    d.ref->load(vu);
    d.ref->run(address);
    d.next = vu.sink();
    d.kicks.clear();
    vu.setGifSink(&d);
    cycles += vu.run(address);
    vu.setGifSink(d.next);
    if (tracing) {
        std::fclose(d.ref->trace);
        std::fclose(vu.trace);
        d.ref->trace = vu.trace = nullptr;
    }
    VuDiff::Count& c = d.programs[address];
    ++c.runs;
    const bool kick = d.kicks.size() != d.ref->kicks.size() || wordsDiffer(d.kicks.data(), d.ref->kicks.data(), d.kicks.size());
    const bool mem = wordsDiffer(vu.data, d.ref->data, sizeof vu.data);
    const bool regs = wordsDiffer(reinterpret_cast<const uint8_t*>(vu.vf), reinterpret_cast<const uint8_t*>(d.ref->vf), sizeof vu.vf) || std::memcmp(vu.vi, d.ref->vi, sizeof vu.vi) != 0;
    c.kick += kick;
    c.mem += mem;
    c.regs += regs;
    if (!(kick || mem || regs) || (c.kick + c.mem + c.regs) > 3 * (kick + mem + regs)) return;
    std::fprintf(stderr, "[vu1 diff] %04X run %llu: PATH1 %zu/%zu bytes\n", address * 8, (unsigned long long)c.runs, d.kicks.size(),
                 d.ref->kicks.size());
    static bool dumped = false;
    if (!dumped) {   // the micro memory, for disassembly
        dumped = true;
        if (FILE* f = std::fopen("vu1_micro.bin", "wb")) {
            std::fwrite(vu.micro, 1, sizeof vu.micro, f);
            std::fclose(f);
        }
    }
    if (kick) reportQwords("PATH1", address, d.kicks.data(), d.ref->kicks.data(), std::min(d.kicks.size(), d.ref->kicks.size()));
    if (mem) reportQwords("data", address, vu.data, d.ref->data, sizeof vu.data);
    for (int k = 0; k < 32; ++k)
        if (wordsDiffer(reinterpret_cast<const uint8_t*>(&vu.vf[k]), reinterpret_cast<const uint8_t*>(&d.ref->vf[k]), 16)) {
            std::fprintf(stderr, "[vu1 diff] %04X vf%d: ours %08X %08X %08X %08X, reference %08X %08X %08X %08X\n", address * 8, k,
                         vu.vf[k].u[0], vu.vf[k].u[1], vu.vf[k].u[2], vu.vf[k].u[3], d.ref->vf[k].u[0], d.ref->vf[k].u[1],
                         d.ref->vf[k].u[2], d.ref->vf[k].u[3]);
        }
    for (int k = 1; k < 16; ++k)
        if (vu.vi[k] != d.ref->vi[k]) std::fprintf(stderr, "[vu1 diff] %04X vi%d: ours %04X, reference %04X\n", address * 8, k, vu.vi[k], d.ref->vi[k]);
}

// GT4's microprograms run in C++ (vu1_native.cpp) by default; the interpreter runs what the native code
// leaves (another game's micro image, the clipper's rare record overflow). GT4_VU1=interp: everything in
// the interpreter; GT4_VU1=ref: in the reference interpreter. GT4_VU1_DIFF=native: every program the native
// code handles also runs in the reference interpreter, from the same state; the PATH1 packets and the data
// memory must be identical (a table per entry at exit).
Vu1Native* vuNative() {
    static Vu1Native* native = [] {
        const char* mode = std::getenv("GT4_VU1");
        const char* diff = std::getenv("GT4_VU1_DIFF");
        if (diff && !std::strcmp(diff, "native")) return new Vu1Native;
        if (diff || (mode && (!std::strcmp(mode, "interp") || !std::strcmp(mode, "ref")))) return static_cast<Vu1Native*>(nullptr);
        return new Vu1Native;
    }();
    return native;
}

struct NativeDiff final : GifSink {
    struct Collect final : GifSink {
        std::vector<uint8_t> kicks;
        GifSink* next = nullptr;
        void gif(uint32_t path, const uint8_t* data, size_t bytes) override {
            if (path == 1) kicks.insert(kicks.end(), data, data + bytes);
            if (next) next->gif(path, data, bytes);
        }
    };
    void gif(uint32_t, const uint8_t*, size_t) override {}
    Vu1 scratch;
    std::unique_ptr<Vu1Ref> ref = std::make_unique<Vu1Ref>();
    struct Count { uint64_t runs = 0, kick = 0, mem = 0; };
    std::map<uint32_t, Count> entries;
    ~NativeDiff() override {
        std::fprintf(stderr, "[vu1 native] entry: runs, PATH1 differs, data differs\n");
        for (const auto& [address, c] : entries)
            std::fprintf(stderr, "[vu1 native] %04X: %llu %llu %llu\n", address * 8, (unsigned long long)c.runs, (unsigned long long)c.kick,
                         (unsigned long long)c.mem);
        if (Vu1Native* native = vuNative())
            std::fprintf(stderr, "[vu1 native] %llu native, %llu interpreted (%llu needed the polygon clipper)\n",
                         (unsigned long long)native->stats.native, (unsigned long long)native->stats.interpreted,
                         (unsigned long long)native->stats.clipperFallbacks);
        if (Vu1Native* native = vuNative())
            for (const auto& [part, runs] : native->stats.parts)
                std::fprintf(stderr, "[vu1 native] part %04X: %llu runs\n", part, (unsigned long long)runs);
    }
};

NativeDiff* nativeDiff() {
    static NativeDiff* diff = [] {
        const char* mode = std::getenv("GT4_VU1_DIFF");
        return mode && !std::strcmp(mode, "native") ? new NativeDiff : nullptr;
    }();
    static struct Free { ~Free() { delete nativeDiff(); } } freeAtExit;
    return diff;
}

// Runs the program natively when it can and compares with the reference interpreter (vu1_ref.cpp, whose
// float arithmetic the native code shares); the frame itself still comes from vu1.cpp. False: not handled
// natively (the caller interprets it).
bool runNativeDiff(NativeDiff& d, Vu1Native& native, Vu1& vu, uint32_t address, uint64_t& cycles) {
    d.scratch = vu;
    NativeDiff::Collect nativeKicks;
    d.scratch.setGifSink(&nativeKicks);
    if (!native.run(d.scratch, address)) return false;
    d.ref->load(vu);
    d.ref->sink = nullptr;
    d.ref->run(address);
    cycles += vu.run(address);
    NativeDiff::Collect interpreted;
    interpreted.kicks = d.ref->kicks;
    NativeDiff::Count& c = d.entries[address];
    ++c.runs;
    const bool kick = nativeKicks.kicks != interpreted.kicks;
    // Qwords 871..1023 (and 870's z, w) are the clipper's scratch area, beyond both VIF buffers (TOP 0 and
    // 435): every program writes what it reads there, so only the packet start in 870 x, y is compared.
    const bool mem = std::memcmp(d.scratch.data, d.ref->data, 870 * 16 + 8) != 0;
    c.kick += kick;
    c.mem += mem;
    if ((kick || mem) && c.kick + c.mem <= 6) {
        std::fprintf(stderr, "[vu1 native] %04X run %llu: PATH1 %zu/%zu bytes\n", address * 8, (unsigned long long)c.runs,
                     nativeKicks.kicks.size(), interpreted.kicks.size());
        auto show = [&](const char* what, const uint8_t* ours, const uint8_t* theirs, size_t bytes, uint32_t base) {
            int shown = 0;
            for (size_t q = 0; q * 16 < bytes && shown < 6; ++q) {
                if (!std::memcmp(ours + q * 16, theirs + q * 16, 16)) continue;
                uint32_t a[4], b[4];
                std::memcpy(a, ours + q * 16, 16);
                std::memcpy(b, theirs + q * 16, 16);
                std::fprintf(stderr, "[vu1 native] %s qword %zu: native %08X %08X %08X %08X, interpreter %08X %08X %08X %08X\n", what,
                             base + q, a[0], a[1], a[2], a[3], b[0], b[1], b[2], b[3]);
                ++shown;
            }
        };
        if (kick) show("PATH1", nativeKicks.kicks.data(), interpreted.kicks.data(), std::min(nativeKicks.kicks.size(), interpreted.kicks.size()), 0);
        if (mem) show("data", d.scratch.data, d.ref->data, sizeof vu.data, 0);
        std::fprintf(stderr, "[vu1 native] TOP %u\n", vu.top);
    }
    return true;
}

constexpr uint32_t cmdOf(uint32_t code) { return (code >> 24) & 0x7F; }

// Bytes of one UNPACK element: (32 >> vl) * (vn + 1) / 8; V4-5 is 2.
uint32_t elementBytes(uint32_t cmd) {
    const uint32_t vn = (cmd >> 2) & 3, vl = cmd & 3;
    if (vn == 3 && vl == 3) return 2;
    return ((32u >> vl) * (vn + 1)) / 8;
}

}  // namespace

Vif1::Vif1(Vu1& unit) : vu(unit) {}

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
        case 0x14: case 0x15: execute(imm); break;                           // MSCAL MSCALF
        case 0x17: execute(vu.pc()); break;                                  // MSCNT
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
                std::memcpy(vu.micro + ((imm + k) & 0x7FF) * 8, next + 2 * k, 8);
            if (Vu1Inventory* inventory = Vu1Inventory::get()) inventory->microWritten();
            if (Vu1Native* native = vuNative()) native->microWritten();
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
        default:
            ++stats.unknownCodes;
            lastWarning = "unknown VIF1 code " + std::to_string(cmd);
            break;
        }
    }
    pos += consumed;
    return true;
}

void Vif1::execute(uint32_t address) {
    // The VU sees ITOPS and the current TOPS; the double buffer flips for the next transfer.
    vu.itop = itops;
    vu.top = tops & 0x3FF;
    if (dbf) {
        tops = base;
        dbf = false;
    } else {
        tops = base + ofst;
        dbf = true;
    }
    lastProgram = address * 8;
    ++stats.microprograms;
    Vu1Native* native = vuNative();
#if GT4_VU1_INTERPRETER
    Vu1Inventory* inventory = Vu1Inventory::get();
    const uint64_t cyclesBefore = stats.vuCycles;
    if (inventory) inventory->before(vu, address);
    static Vu1Ref* reference = [] {
        const char* mode = std::getenv("GT4_VU1");
        return mode && !std::strcmp(mode, "ref") ? new Vu1Ref : nullptr;
    }();
    if (NativeDiff* nd = nativeDiff(); nd && runNativeDiff(*nd, *native, vu, address, stats.vuCycles)) {
    } else if (native && !nd && native->run(vu, address)) {
    } else if (reference) {   // GT4_VU1=ref: the reference interpreter runs the program
        reference->load(vu);
        reference->sink = vu.sink();
        reference->run(address);
        reference->store(vu);
    } else if (VuDiff* diff = vuDiff()) {
        runWithDiff(*diff, vu, address, stats.vuCycles);
    } else {
        stats.vuCycles += vu.run(address);
    }
    if (inventory) inventory->after(vu, stats.vuCycles - cyclesBefore);
    if (vu.runaway()) {
        ++stats.runaways;
        lastWarning = "VU1 program at " + std::to_string(address) + " did not end";
    }
#else
    // Without the interpreter only GT4's native programs run; anything else draws nothing.
    if (!native || !native->run(vu, address)) {
        ++stats.skippedPrograms;
        lastWarning = "VU1 program at " + std::to_string(address * 8) + " has no native version";
    }
#endif
}

void Vif1::unpack(uint32_t code, const uint32_t* source) {
    const uint32_t cmd = cmdOf(code);
    const uint32_t vn = (cmd >> 2) & 3, vl = cmd & 3;
    const bool masked = (cmd >> 4) & 1;
    const bool usn = (code >> 14) & 1;
    uint32_t num = (code >> 16) & 0xFF;
    if (!num) num = 256;
    uint32_t address = code & 0x3FF;
    if ((code >> 15) & 1) address += tops;
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
        uint32_t* destination = reinterpret_cast<uint32_t*>(vu.data + (address & 0x3FF) * 16);
        const uint32_t maskCycle = std::min(cycle, 3u);
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
                break;
            case 1: destination[f] = row[f]; break;
            case 2: destination[f] = col[maskCycle]; break;
            default: break;   // write protected
            }
        }
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
