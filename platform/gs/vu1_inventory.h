#pragma once
// GT4_VU1_INVENTORY=DIR: records every VU1 microprogram the game runs, for porting them to native code.
//
// A program is identified by its micro memory image (FNV-1a of the 16 KiB, rehashed after an MPG changed
// it) and its start address. Per program: runs, VU cycles, the instructions executed (union over runs),
// the PATH1 packets it kicks and the GIFtag shapes in them (PRIM and register list). At exit DIR receives
// inventory.tsv and one micro image per hash (images/HASH.bin: game data, keep it out of git).
#include "vu1.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace gt4::ps2 {

class Vu1Inventory final : GifSink {
public:
    static Vu1Inventory* get();   // null unless GT4_VU1_INVENTORY is set
    explicit Vu1Inventory(std::string dir);
    ~Vu1Inventory() override;

    void microWritten() { imageKnown = false; }
    void before(Vu1& vu, uint32_t pc);
    void after(Vu1& vu, uint64_t cycles);

private:
    struct Program {
        uint64_t runs = 0, cycles = 0, kicks = 0, kickBytes = 0, maxKickBytes = 0;
        std::array<uint8_t, 2048> covered{};
        std::map<std::string, uint64_t> shapes;   // GIFtag shape -> count
    };
    void gif(uint32_t path, const uint8_t* data, size_t bytes) override;
    void write();

    std::string dir;
    std::map<uint64_t, std::vector<uint8_t>> images;
    std::map<std::pair<uint64_t, uint32_t>, Program> programs;
    uint64_t image = 0;
    bool imageKnown = false;
    Program* current = nullptr;
    GifSink* next = nullptr;
    std::array<uint8_t, 2048> coverage{};
    std::chrono::steady_clock::time_point lastWrite = std::chrono::steady_clock::now();
};

}  // namespace gt4::ps2
