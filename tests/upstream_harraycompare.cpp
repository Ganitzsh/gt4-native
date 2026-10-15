#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>

struct func_005C9930_a0 { char pad0[0x174]; std::int32_t unk174; };
struct func_005C9970_arg0 { char pad0[0x174]; std::int32_t unk174; };
static_assert(offsetof(func_005C9930_a0, unk174) == 0x174);
static_assert(offsetof(func_005C9970_arg0, unk174) == 0x174);
extern "C" void func_005C9930(func_005C9930_a0*, std::int32_t);
float func_005C9970(func_005C9970_arg0*);

// The only unresolved original dependency: source of the divisor.
// This stub is explicit, deterministic and can be replaced by the real game function.
namespace { float divisor = 4.0f; int divisor_calls = 0; }
float func_001465D8() { ++divisor_calls; return divisor; }

#define CHECK(expr) do { if (!(expr)) { std::cerr << "GT4 object chain failed: " #expr << '\n'; return 1; } } while(false)
int main() {
    alignas(std::int32_t) std::array<std::byte, 0x178> storage{};
    auto* writer = reinterpret_cast<func_005C9930_a0*>(storage.data());
    auto* reader = reinterpret_cast<func_005C9970_arg0*>(storage.data());
    func_005C9930(writer, 120);
    CHECK(func_005C9970(reader) == 30.0f);
    CHECK(divisor_calls == 1);
    func_005C9930(writer, -18);
    divisor = 3.0f;
    CHECK(func_005C9970(reader) == -6.0f);
    CHECK(divisor_calls == 2);
    std::int32_t stored = 0;
    std::memcpy(&stored, storage.data() + 0x174, sizeof(stored));
    CHECK(stored == -18);
    std::cout << "Two original GT4 functions share one native object: passed\n";
}
