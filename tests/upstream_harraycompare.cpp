#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>

struct func_005C9930_a0 { char pad0[0x174]; std::int32_t unk174; };
struct func_005C9970_arg0 { char pad0[0x174]; std::int32_t unk174; };
struct Obj001465D8 { char pad0[0x178]; std::int32_t unk178; };
static_assert(offsetof(func_005C9930_a0, unk174) == 0x174);
static_assert(offsetof(func_005C9970_arg0, unk174) == 0x174);
static_assert(offsetof(Obj001465D8, unk178) == 0x178);
extern "C" void func_005C9930(func_005C9930_a0*, std::int32_t);
float func_005C9970(func_005C9970_arg0*);

// The decompiled reader declares func_001465D8() with no arguments, but the
// real definition takes an object pointer. This test bridge supplies the
// missing context; it is NOT evidence that the original ABI is resolved.
extern "C" float gt4_original_func_001465D8(Obj001465D8*);
namespace { Obj001465D8 divisor_object{}; int divisor_calls = 0; }
float func_001465D8() {
    ++divisor_calls;
    return gt4_original_func_001465D8(&divisor_object);
}

#define CHECK(expr) do { if (!(expr)) { std::cerr << "GT4 object chain failed: " #expr << '\n'; return 1; } } while(false)
int main() {
    alignas(std::int32_t) std::array<std::byte, 0x178> storage{};
    divisor_object.unk178 = 400;
    auto* writer = reinterpret_cast<func_005C9930_a0*>(storage.data());
    func_005C9970_arg0 reader{};
    auto transfer = [&] { std::memcpy(&reader, storage.data(), storage.size()); };
    func_005C9930(writer, 120);
    transfer();
    CHECK(func_005C9970(&reader) == 30.0f);
    CHECK(divisor_calls == 1);
    func_005C9930(writer, -18);
    divisor_object.unk178 = 300;
    transfer();
    CHECK(func_005C9970(&reader) == -6.0f);
    CHECK(divisor_calls == 2);
    std::int32_t stored = 0;
    std::memcpy(&stored, storage.data() + 0x174, sizeof(stored));
    CHECK(stored == -18);
    std::cout << "Three original GT4 functions integrated through explicit ABI bridge: passed\n";
}
