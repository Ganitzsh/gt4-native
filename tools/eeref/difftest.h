#pragma once
// Differential tests of native rewrites against the original code (difftest.cpp; GT4_DIFFTEST=NAME,...|all in a
// gt4_game linked with `game_link.py --eeorig`). A file tools/eeref/difftest*.cpp registers its tests with
//
//     static const difftest::Register r1({"003E21D8", 0x003E21D8, "ii", 'v', true, rotate_point});
//
// where the setup function fills the scratch area of guest RAM (and the shared vectors the function reads) with
// random inputs and gives the arguments; both versions then run from that state, and what they leave is compared
// (difftest.cpp, run(): the scratch area, the matrix stack, the scratchpad, the test's data region, shared vectors
// 8..31, the matrix stack's top, the random state) with the return (floats within 1e-5, or 1e-3 counted as drift,
// when `floats`). A function that sends a VIF1 packet (Test::stream, sends_packet()) is compared by what the packet
// delivers, since the native sends native records where the original sent VIF codes and GIF data: the batches run and
// the batch buffers (through the host's decoder, platform/gs/vif1), the vertices drawn, the other records byte for
// byte. GT4_DIFFTEST_DUMP=1 logs the packets of a test's first trials that cannot be compared (=all: any packets).
// The native call is not guarded: a test that crashes ends the process before the tests after it
// report (and the order of tests from different files is not fixed), so run a suspect alone (GT4_DIFFTEST=NAME).
// Set GT4_ORIG_KINDS=build/eeref/kinds.txt (tools/eeref/kinds.py): without it the bridge calls every native function
// from the original code with eight integers and takes a 32-bit result, so a double through a native soft-float
// routine loses its high half and the original side computes garbage (005570E0: 200 of 200 differ; with it, 0).
#include <cstdint>

namespace difftest {

constexpr std::uint32_t kScratch = 0x01800000, kScratchSize = 0x00200000;

struct Test {
    const char *name;    // the GT4_DIFFTEST name (usually the address)
    std::uint32_t addr;  // the PS2 function
    const char *kinds;   // its parameters as the bridge passes them to the original: i (32-bit), u (32-bit, zero-extended), w (64-bit), f, q
    char ret;            // v, i, w (64-bit), f; 's' a 16-byte structure (native: hidden pointer; not compared)
    bool floats;         // outputs are floats computed differently (VU arithmetic against x87)
    void (*setup)(std::uint32_t args[8]);   // a float argument as its bits (bits())
    std::uint32_t data_addr = 0, data_size = 0;   // game data the function writes, compared too (saved and restored)
    int loose_vector = -1, loose_lanes = 0;        // lanes (x 1, y 2, z 4, w 8) of one shared vector not compared:
                                                   // what the original leaves there is a register's stale bits
    void *native = nullptr;   // another native version to run instead of the linked one (an older draft, a
                              // candidate fix) under its own name; the original side is the same either way
    std::uint32_t stream = 0;        // the packet cursor's address, for a function that sends a VIF1 packet from the
                                     // cursor on: the original sends VIF codes, the native the same batches as native
                                     // records (include/gt4/gfx.h). Both packets go through the host's decoder
                                     // (platform/gs/vif1.h) and what they deliver is compared (the batches run, the
                                     // batch buffers), not their bytes or where the cursor ends (difftest.cpp,
                                     // deliver())
    std::uint32_t stream_mask = 0;   // the address of the VIF mask's shadow (the graphics library's +0x40): the
                                     // decoders start with that mask, as the hardware held it (0: the frame head's)
    bool stream_gif = false;         // the original appends GIF data inside a DIRECT its caller opened (no DMA tag
                                     // of its own): its bytes from the cursor are read as GIF, the native's as above
    std::uint32_t stream_native = 0;   // the native's packet cursor when it sends elsewhere (0: `stream`): a native
                                       // that draws through the graphics library's packet (0x700021F8)
    std::uint32_t double_addr = 0, double_size = 0;   // a region of doubles (8-byte steps from double_addr), compared
                                                      // as doubles: within 1e-12 relative is rounding, 1e-6 drift,
                                                      // when `floats` (the EE's soft-float against x87 doubles)
};

// t with its packet compared as what it delivers (Test::stream, Test::stream_mask)
inline Test sends_packet(Test t, std::uint32_t cursor, std::uint32_t mask_shadow = 0) {
    t.stream = cursor;
    t.stream_mask = mask_shadow;
    return t;
}
// t with GIF data appended at the cursor by the original, compared as what it draws (Test::stream_gif)
inline Test appends_gif(Test t, std::uint32_t cursor, std::uint32_t native_cursor = 0) {
    t.stream = cursor;
    t.stream_gif = true;
    t.stream_native = native_cursor;
    return t;
}

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
// shared vectors first..last, every lane random in [lo, hi]
void random_vectors(int first, int last, float lo, float hi);
float *vec(int n);

}  // namespace difftest
