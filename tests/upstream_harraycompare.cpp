#include "gt4/native/object_fields.hpp"
#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>

template<class Exception, class F> bool throws(F&& f) {
    try { f(); } catch (const Exception&) { return true; }
    return false;
}

int main() {
    namespace fields = gt4::native::object_fields;
    using fields::Function;
    using gt4::guest::Address;
    static_assert(sizeof(fields::Object) == 4);
    std::array<std::byte, fields::Layout::wire_size> first{}, second{}, rom{};
    gt4::guest::AddressSpace memory;
    constexpr Address a = 0x00130000, b = 0x00200000, r = 0x00300000;
    memory.map(a, first); memory.map(b, second); memory.map(r, rom, false);
    gt4::guest::NativeCallbacks callbacks;
    fields::bind_callbacks(callbacks, memory);
    const auto invoke = [&](Function function, Address object, std::int32_t argument = 0) {
        return callbacks.call(static_cast<Address>(function), object, std::bit_cast<std::uint32_t>(argument));
    };
    const auto result = [&](Address object) {
        return std::bit_cast<float>(invoke(Function::normalized, object));
    };
    (void)invoke(Function::set_174, a, 120);
    (void)invoke(Function::set_178, a, 400);
    (void)invoke(Function::set_174, b, -18);
    (void)invoke(Function::set_178, b, 300);
    assert(result(a) == 30.0f && result(b) == -6.0f);
    assert(result(a) == 30.0f); // no global or separately supplied divisor object
    assert(std::bit_cast<float>(invoke(Function::divisor, a)) == 4.0f);
    assert(std::bit_cast<std::int32_t>(invoke(Function::get_174, b)) == -18);
    assert(invoke(Function::get_178, a) == 400);
    assert(memory.read8(a + 0x174) == 120);
    assert(memory.read8(a + 0x178) == 0x90 && memory.read8(a + 0x179) == 1);
    (void)invoke(Function::set_17c, a, std::numeric_limits<std::int32_t>::min());
    (void)invoke(Function::set_180, a, std::numeric_limits<std::int32_t>::max());
    (void)invoke(Function::set_188, a, -77);
    fields::Object(a).field<std::int32_t>(0x184).store(memory, 42);
    assert(std::bit_cast<std::int32_t>(invoke(Function::get_17c, a)) == std::numeric_limits<std::int32_t>::min());
    assert(std::bit_cast<std::int32_t>(invoke(Function::get_180, a)) == std::numeric_limits<std::int32_t>::max());
    assert(std::bit_cast<std::int32_t>(invoke(Function::get_188, a)) == -77);
    assert(invoke(Function::get_184, a) == 42);
    const auto snapshot = first;
    (void)result(a);
    assert(first == snapshot);
    assert(fields::get_174(memory, fields::Object(r)) == 0);
    assert(throws<std::runtime_error>([&] { (void)invoke(Function::set_174, r, 1); }));
    assert(rom == decltype(rom){});
    assert(throws<std::out_of_range>([&] { (void)result(0); }));
    assert(throws<std::out_of_range>([&] { (void)result(a + 1); }));
    assert(throws<std::out_of_range>([&] { (void)result(a + 4); }));
    assert(throws<std::out_of_range>([&] { (void)result(0x00400000); }));
    assert(throws<std::invalid_argument>([&] { fields::bind_callbacks(callbacks, memory); }));
    std::uint32_t state = 1234567;
    for (unsigned i = 0; i < 10000; ++i) {
        state = state * 1664525u + 1013904223u;
        const auto value = std::int32_t(state % 200001u) - 100000;
        const auto denominator = std::int32_t((state >> 16) % 10000u) + 1;
        fields::set_174(memory, fields::Object(a), value);
        fields::set_178(memory, fields::Object(a), denominator);
        assert(result(a) == float(value) / (float(denominator) / 100.0f));
        assert(result(b) == -6.0f);
    }
    fields::set_174(memory, fields::Object(a), 1);
    fields::set_178(memory, fields::Object(a), 0);
    assert(std::isinf(result(a))); // documented host IEEE behavior, not PS2 emulation
}
