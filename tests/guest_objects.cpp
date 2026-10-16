#include "gt4/guest_field.hpp"
#include "gt4/guest_span.hpp"
#include <array>
#include <bit>
#include <cassert>
#include <cstdint>
#include <limits>

template<class Exception, class F> bool throws(F&& f) {
    try { f(); } catch (const Exception&) { return true; }
    return false;
}

struct Record {
    static constexpr std::size_t wire_size = 16;
    static constexpr std::size_t wire_alignment = 4;
};

int main() {
    using namespace gt4::guest;
    static_assert(sizeof(guest_ptr<Record>) == 4 && sizeof(guest_ptr<float>) == 4);
    static_assert(wire_scalar<float> && wire_scalar<double> && !wire_scalar<bool>);
    static_assert(std::is_same_v<decltype(guest_ptr<const Record>(0x1000).field<float>(4)),
                                 guest_ptr<const float>>);
    std::array<std::byte, 64> ram{};
    std::array<std::byte, 16> top{}, readonly{};
    AddressSpace memory;
    memory.map(0x1000, ram); memory.map(0xFFFFFFF0u, top); memory.map(0x2000, readonly, false);
    const guest_ptr<Record> base(0x1000);
    const guest_span<Record> records(base, 3);
    assert(records.size() == 3 && !records.empty());
    assert(records.at(2).address() == 0x1020);
    const auto first_value = records.at(0).field<float>(4);
    const auto second_value = records.at(1).field<float>(4);
    first_value.store(memory, -1.5f);
    assert(first_value.load(memory) == -1.5f);
    assert(memory.read32_le(0x1004) == 0xBFC00000u);
    records.at(2).field<double>(8).store(memory, -123.25);
    assert(records.at(2).field<double>(8).load(memory) == -123.25);
    assert(memory.read32_le(0x1028) == 0 && memory.read32_le(0x102C) == 0xC05ED000u);
    const auto pointer = base.field<Address>(0);
    pointer.store_pointer(memory, records.at(2));
    const auto linked = pointer.load_pointer<Record>(memory);
    assert(linked.address() == 0x1020);
    assert(linked.field<double>(8).load(memory) == -123.25);
    assert(memory.read32_le(0x1000) == 0x1020); // four bytes on a 64-bit host
    guest_field<float> x(memory, first_value), y(memory, second_value);
    y = x;
    assert(second_value.load(memory) == -1.5f);
    x = 7.0f;
    assert(second_value.load(memory) == -1.5f); // assignment did not rebind y
    guest_field<float> immutable(std::as_const(memory), first_value);
    assert(throws<std::runtime_error>([&] { immutable = 0.0f; }));
    for (std::uint32_t bits : {0u, 0x80000000u, 0x7F800000u, 0x7FC12345u, 0x00000001u}) {
        first_value.store(memory, std::bit_cast<float>(bits));
        assert(std::bit_cast<std::uint32_t>(first_value.load(memory)) == bits);
    }
    const guest_span<std::uint32_t> integers(guest_ptr<std::uint32_t>(0x1030), 4);
    integers.at(3).store(memory, 0xFEDCBA98u);
    assert(memory.read32_le(0x103C) == 0xFEDCBA98u);
    const guest_ptr<const std::uint32_t> const_word(0x103C);
    assert(const_word.load(memory) == 0xFEDCBA98u);
    guest_ptr<double>(0xFFFFFFF8u).store(memory, 0.5);
    assert(guest_ptr<double>(0xFFFFFFF8u).load(memory) == 0.5);
    assert(guest_span<Record>(guest_ptr<Record>{}, 0).empty());
    assert(throws<std::out_of_range>([&] { (void)records.at(3); }));
    assert(throws<std::out_of_range>([&] { (void)guest_span<Record>(base, std::numeric_limits<std::size_t>::max()); }));
    assert(throws<std::out_of_range>([&] { (void)guest_span<Record>(guest_ptr<Record>{}, 1); }));
    assert(throws<std::out_of_range>([&] { (void)base.add(std::numeric_limits<std::size_t>::max()); }));
    assert(throws<std::out_of_range>([&] { (void)guest_ptr<Record>{}.field<float>(4); }));
    assert(throws<std::out_of_range>([&] { (void)base.field<double>(12); }));
    assert(throws<std::out_of_range>([&] { (void)guest_ptr<Record>(0x1001).field<float>(3); }));
    assert(throws<std::out_of_range>([&] { (void)guest_ptr<std::uint32_t>(0xFFFFFFFCu).add(1); }));
    assert(throws<std::out_of_range>([&] { (void)guest_ptr<float>(0x1040).load(memory); }));
    assert(throws<std::out_of_range>([&] { (void)guest_ptr<double>(0x103C).load(memory); }));
    assert(throws<std::runtime_error>([&] { guest_ptr<float>(0x2000).store(memory, 1.0f); }));
}
