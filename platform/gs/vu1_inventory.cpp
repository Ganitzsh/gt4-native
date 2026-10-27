// VU1 microprogram inventory (see vu1_inventory.h).
#include "vu1_inventory.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace gt4::ps2 {

namespace {

uint64_t fnv(const uint8_t* data, size_t bytes, uint64_t h = 0xCBF29CE484222325ull) {
    for (size_t k = 0; k < bytes; ++k) h = (h ^ data[k]) * 0x100000001B3ull;
    return h;
}

const char* regName(uint32_t r) {
    static const char* names[16] = {"PRIM", "RGBAQ", "ST", "UV", "XYZF2", "XYZ2", "TEX0_1", "TEX0_2",
                                    "CLAMP_1", "CLAMP_2", "FOG", "R11", "XYZF3", "XYZ3", "AD", "NOP"};
    return names[r & 15];
}

// The shape of each GIFtag in a packet: FLG, PRIM when PRE is set, the register list; for A+D the
// GS register addresses written.
void shapes(const uint8_t* data, size_t bytes, std::map<std::string, uint64_t>& out) {
    size_t q = 0;
    const size_t total = bytes / 16;
    while (q < total) {
        uint64_t lo, hi;
        std::memcpy(&lo, data + q * 16, 8);
        std::memcpy(&hi, data + q * 16 + 8, 8);
        ++q;
        const uint32_t nloop = lo & 0x7FFF;
        const bool eop = (lo >> 15) & 1;
        const bool pre = (lo >> 46) & 1;
        const uint32_t prim = (lo >> 47) & 0x7FF;
        const uint32_t flg = (lo >> 58) & 3;
        uint32_t nreg = (lo >> 60) & 0xF;
        if (!nreg) nreg = 16;
        char buf[256];
        int n = std::snprintf(buf, sizeof buf, "%s", flg == 0 ? "P" : flg == 1 ? "R" : "I");
        if (pre) n += std::snprintf(buf + n, sizeof buf - n, " prim=%03X", prim);
        if (flg == 0 || flg == 1) {
            n += std::snprintf(buf + n, sizeof buf - n, " ");
            for (uint32_t r = 0; r < nreg && n < 200; ++r)
                n += std::snprintf(buf + n, sizeof buf - n, "%s%s", r ? "," : "", regName(uint32_t(hi >> (4 * r))));
        }
        const uint32_t count = flg == 0 ? nloop * nreg : flg == 1 ? (nloop * nreg + 1) / 2 : nloop;
        std::string shape = buf;
        if (flg == 0 && nreg == 1 && (hi & 15) == 14) {   // A+D: the GS registers written
            std::set<uint32_t> regs;
            for (uint32_t k = 0; k < nloop && q + k < total; ++k) regs.insert(data[(q + k) * 16 + 8]);
            shape += " [";
            for (uint32_t r : regs) {
                std::snprintf(buf, sizeof buf, "%02X ", r);
                shape += buf;
            }
            shape.back() = ']';
        }
        ++out[shape];
        q += count;
        if (eop) break;
    }
}

}  // namespace

Vu1Inventory* Vu1Inventory::get() {
    static Vu1Inventory* inventory = std::getenv("GT4_VU1_INVENTORY") ? new Vu1Inventory(std::getenv("GT4_VU1_INVENTORY")) : nullptr;
    static struct Free { ~Free() { delete get(); } } freeAtExit;
    return inventory;
}

Vu1Inventory::Vu1Inventory(std::string directory) : dir(std::move(directory)) {}

Vu1Inventory::~Vu1Inventory() { write(); }

void Vu1Inventory::before(Vu1& vu, uint32_t pc) {
    if (!imageKnown) {
        image = fnv(vu.micro, sizeof vu.micro);
        if (!images.count(image)) images[image].assign(vu.micro, vu.micro + sizeof vu.micro);
        imageKnown = true;
    }
    current = &programs[{image, pc & 0x7FF}];
    coverage.fill(0);
    vu.coverage = coverage.data();
    next = vu.sink();
    vu.setGifSink(this);
}

void Vu1Inventory::after(Vu1& vu, uint64_t cycles) {
    vu.setGifSink(next);
    vu.coverage = nullptr;
    ++current->runs;
    current->cycles += cycles;
    for (size_t k = 0; k < coverage.size(); ++k) current->covered[k] |= coverage[k];
    current = nullptr;
    // Written every 20 s as well as at exit: a host stopped from outside still leaves the inventory.
    const auto now = std::chrono::steady_clock::now();
    if (now - lastWrite > std::chrono::seconds(20)) {
        lastWrite = now;
        write();
    }
}

void Vu1Inventory::gif(uint32_t path, const uint8_t* data, size_t bytes) {
    if (current && path == 1) {
        ++current->kicks;
        current->kickBytes += bytes;
        current->maxKickBytes = std::max<uint64_t>(current->maxKickBytes, bytes);
        shapes(data, bytes, current->shapes);
    }
    if (next) next->gif(path, data, bytes);
}

void Vu1Inventory::write() {
    std::filesystem::create_directories(std::filesystem::path(dir) / "images");
    for (const auto& [hash, bytes] : images) {
        char name[64];
        std::snprintf(name, sizeof name, "images/%016llX.bin", static_cast<unsigned long long>(hash));
        if (FILE* f = std::fopen((std::filesystem::path(dir) / name).string().c_str(), "wb")) {
            std::fwrite(bytes.data(), 1, bytes.size(), f);
            std::fclose(f);
        }
    }
    FILE* f = std::fopen((std::filesystem::path(dir) / "inventory.tsv").string().c_str(), "w");
    if (!f) return;
    // code: FNV-1a of the executed instructions with their addresses, the same for one program in two images
    std::fprintf(f, "image\tstart\tcode\truns\tcycles\tcycles_per_run\tinstructions\tfirst\tlast\tkicks\tkick_bytes\tmax_kick\tranges\tshapes\n");
    for (const auto& [key, p] : programs) {
        const std::vector<uint8_t>& micro = images[key.first];
        uint64_t code = 0xCBF29CE484222325ull;
        uint32_t count = 0, first = 0x7FF, last = 0;
        for (uint32_t k = 0; k < 2048; ++k) {
            if (!p.covered[k]) continue;
            code = fnv(reinterpret_cast<const uint8_t*>(&k), 4, code);
            code = fnv(micro.data() + k * 8, 8, code);
            ++count;
            first = std::min(first, k);
            last = std::max(last, k);
        }
        std::string ranges;   // executed instructions as byte-address ranges
        for (uint32_t k = 0; k < 2048; ++k) {
            if (!p.covered[k] || (k && p.covered[k - 1])) continue;
            uint32_t end = k;
            while (end + 1 < 2048 && p.covered[end + 1]) ++end;
            char range[32];
            std::snprintf(range, sizeof range, "%s%04X-%04X", ranges.empty() ? "" : ",", k * 8, end * 8);
            ranges += range;
        }
        std::string shapeList;
        for (const auto& [shape, n] : p.shapes) shapeList += (shapeList.empty() ? "" : "; ") + shape + " x" + std::to_string(n);
        std::fprintf(f, "%016llX\t%04X\t%016llX\t%llu\t%llu\t%llu\t%u\t%04X\t%04X\t%llu\t%llu\t%llu\t%s\t%s\n",
                     static_cast<unsigned long long>(key.first), key.second * 8, static_cast<unsigned long long>(code),
                     static_cast<unsigned long long>(p.runs), static_cast<unsigned long long>(p.cycles),
                     static_cast<unsigned long long>(p.runs ? p.cycles / p.runs : 0), count, first * 8, last * 8,
                     static_cast<unsigned long long>(p.kicks), static_cast<unsigned long long>(p.kickBytes),
                     static_cast<unsigned long long>(p.maxKickBytes), ranges.c_str(), shapeList.c_str());
    }
    std::fclose(f);
}

}  // namespace gt4::ps2
