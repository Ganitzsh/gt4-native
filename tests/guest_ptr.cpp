#include "gt4/guest_ptr.hpp"
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

template<class Exception, class F> bool throws(F&& f) {
    try { f(); } catch (const Exception&) { return true; }
    return false;
}

int main() {
    std::array<std::byte, 16> ram{};
    std::array<std::byte, 4> rom{};
    gt4::guest::AddressSpace memory;
    memory.map(0x00120000, ram);
    memory.map(0x00200000, rom, false);

    const gt4::guest::guest_ptr<std::int32_t> value{0x00120004};
    assert(value.address() == 0x00120004 && !value.is_null());
    value.store(memory, -7);
    assert(memory.read8(0x00120004) == 0xF9);
    assert(memory.read8(0x00120005) == 0xFF);
    assert(value.load(memory) == -7);

    const gt4::guest::guest_ptr<std::uint32_t> word{0x00120008};
    word.store(memory, 0x12345678u);
    assert(word.load(memory) == 0x12345678u);
    assert(memory.read8(0x00120008) == 0x78);
    assert(memory.read8(0x0012000B) == 0x12);

    assert(throws<std::runtime_error>([&] {
        gt4::guest::guest_ptr<std::uint32_t>{0x00200000}.store(memory, 1);
    }));
    assert(throws<std::out_of_range>([&] {
        (void)gt4::guest::guest_ptr<std::int32_t>{0x00120005}.load(memory);
    }));
    assert(throws<std::out_of_range>([&] {
        (void)gt4::guest::guest_ptr<std::int32_t>{0x00120010}.load(memory);
    }));
    assert(throws<std::out_of_range>([&] {
        (void)gt4::guest::guest_ptr<std::int32_t>{0x00130000}.load(memory);
    }));
    assert(throws<std::out_of_range>([&] {
        (void)gt4::guest::guest_ptr<std::int32_t>{}.load(memory);
    }));
}
