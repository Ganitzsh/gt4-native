#include "gt4/native_callbacks.hpp"
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

int main() {
    std::array<std::byte, 256> storage{};
    gt4::guest::MemoryView memory{0x1000u, storage};
    constexpr gt4::guest::Address object = 0x1020u;
    constexpr gt4::guest::Address function = 0x00401234u;
    memory.write32_le(object + 4, 0x1080u);
    // Entry zero is RTTI; method zero lives in entry one.
    memory.write16_le(0x1088u, static_cast<std::uint16_t>(-4));
    memory.write16_le(0x108Au, 0);
    memory.write32_le(0x108Cu, function);

    gt4::guest::NativeCallbacks callbacks;
    callbacks.bind(function, [](auto self, auto value) {
        assert(self == 0x101Cu);
        return value + 7;
    });
    assert(callbacks.contains(function));
    assert(callbacks.call_virtual(memory, object, 0, 35) == 42);

    bool duplicate = false;
    try { callbacks.bind(function, [](auto, auto) { return 0u; }); }
    catch (const std::invalid_argument&) { duplicate = true; }
    assert(duplicate);

    bool missing = false;
    try { (void)callbacks.call(0x9999u, object, 0); }
    catch (const std::out_of_range&) { missing = true; }
    assert(missing);

    memory.write16_le(0x108Au, 1);
    bool unsupported = false;
    try { (void)callbacks.call_virtual(memory, object, 0, 1); }
    catch (const std::runtime_error&) { unsupported = true; }
    assert(unsupported);
}
