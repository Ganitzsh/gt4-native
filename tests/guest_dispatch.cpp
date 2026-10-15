#include "gt4/guest_dispatch.hpp"
#include <array>
#include <cassert>
#include <cstddef>
#include <stdexcept>
int main() {
    std::array<std::byte,256> data{};
    gt4::guest::MemoryView m{0x1000, data};
    m.write32_le(0x1024,0x1080);
    m.write16_le(0x1088,0xFFFC);
    m.write16_le(0x108A,0x0000);
    m.write32_le(0x108C,0x00401234);
    auto c=gt4::guest::resolve_virtual_call(m,0x1020,0);
    assert(c.target==0x00401234 && c.adjusted_this==0x101C && c.index==0);
    bool failed=false;
    try { (void)gt4::guest::resolve_virtual_call(m,0x1020,100); }
    catch (const std::out_of_range&) { failed=true; }
    assert(failed);
}
