// Differential tests of the text engine's layout code (unit_00494578, the network library's text runs); see
// difftest.h.
#include "difftest.h"

#include <cstdint>
#include <cstring>

using namespace difftest;

namespace {

void put32(std::uint32_t at, std::uint32_t v) { std::memcpy(ram(at), &v, 4); }
void put16(std::uint32_t at, std::uint16_t v) { std::memcpy(ram(at), &v, 2); }

// The engine's allocator (func_004C8C20 -> 004C3D70 -> 004C3C90) works on an arena object: +0x10 the 8-byte
// entries (up to 0x2000), +0x14 the free count, +0x18 the ready byte, +0x1C the peak use (func_004C3AD0),
// +0x20 the highest entry. With the ready byte 0 the first allocation sets it up (func_004C3A48), the same way
// on both sides.
void arena(std::uint32_t obj, std::uint32_t entries, std::uint32_t vars) {
    put32(obj + 0x10, entries);
    put32(obj + 0x14, vars);
    put32(obj + 0x18, vars + 4);
    put32(obj + 0x1C, vars + 12);
    put32(obj + 0x20, vars + 8);
    *ram(vars + 4) = 0;
}

// 004C7B38 (owner, text, attrs, priority, lines, item) on an empty text run. func_004C0D30 finds no node in it
// (func_004C1B28 returns at length 0), attrs NULL passes func_004C9580, and the start index comes from the
// character-class scan: the terminator (class 0) stops it at index 0. A draft that merged case 0 with default
// (game_src/func_004C7B38.cpp) gave 1, and 004C7478 then copied length-minus-index = 255 characters into a
// 48-entry stack buffer. The line list (lines, func_004C7140's head) starts empty; priority is NULL or a byte.
void empty_text_run(std::uint32_t a[8]) {
    const std::uint32_t obj = kScratch, vars = kScratch + 0x100, lines = kScratch + 0x110, prio = kScratch + 0x120,
                        text = kScratch + 0x200, entries = kScratch + 0x1000;
    fill(kScratch, 0x12000);
    arena(obj, entries, vars);
    put32(lines, 0);
    put16(text, 0);
    a[0] = obj;
    a[1] = text;
    a[2] = 0;
    a[3] = rnd() % 2 ? prio : 0;
    a[4] = lines;
    a[5] = 0;
}

const Register r1({"004C7B38", 0x004C7B38, "iiiiii", 'i', false, empty_text_run});

}  // namespace
