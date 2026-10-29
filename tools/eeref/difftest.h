#pragma once
// Differential tests of native rewrites against the original code (difftest.cpp; GT4_DIFFTEST=NAME,...|all in a
// gt4_game linked with `game_link.py --eeorig`). A file tools/eeref/difftest*.cpp registers its tests with
//
//     static const difftest::Register r1({"003E21D8", 0x003E21D8, "ii", 'v', true, rotate_point});
//
// where the setup function fills the scratch area of guest RAM (and the shared vectors the function reads) with
// random inputs and gives the arguments; both versions then run from that state, and the scratch area and the
// return are compared (floats within 1e-5, or 1e-3 counted as drift, when `floats`).
#include <cstdint>

namespace difftest {

constexpr std::uint32_t kScratch = 0x01800000, kScratchSize = 0x00200000;

struct Test {
    const char *name;    // the GT4_DIFFTEST name (usually the address)
    std::uint32_t addr;  // the PS2 function
    const char *kinds;   // its parameters as the bridge passes them to the original: i (32-bit), w (64-bit), f, q
    char ret;            // v, i, f; 's' a 16-byte structure (native: hidden pointer; not compared)
    bool floats;         // outputs are floats computed differently (VU arithmetic against x87)
    void (*setup)(std::uint32_t args[8]);   // a float argument as its bits (bits())
};

void add(const Test &t);
struct Register {
    explicit Register(const Test &t) { add(t); }
};

// random numbers and the scratch area
std::uint32_t rnd();
float rndf(float lo, float hi);
std::uint32_t bits(float f);
std::uint8_t *ram(std::uint32_t addr);
float *ramf(std::uint32_t addr);
void fill(std::uint32_t at, std::uint32_t bytes);                                // random bytes
void fill_floats(std::uint32_t at, std::uint32_t count, float lo, float hi);     // random floats
// shared vectors (VU0's registers) first..last, every lane random in [lo, hi]
void random_vectors(int first, int last, float lo, float hi);
float *vec(int n);

}  // namespace difftest
