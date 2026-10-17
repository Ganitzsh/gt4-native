#include "gt4/guest_string.hpp"
#include <array>
#include <cassert>
#include <cstddef>
#include <stdexcept>

int main() {
    std::array<std::byte,128> data{};
    gt4::guest::MemoryView m{0x1000, data};
    m.write32_le(0x1000,0x1040);
    m.write32_le(0x1030,3); // length
    m.write32_le(0x1034,8); // capacity
    m.write32_le(0x1038,2); // ref count
    m.write32_le(0x103C,0); // selfish
    m.write8(0x1040,'G'); m.write8(0x1041,'T'); m.write8(0x1042,'4');
    m.write8(0x1043,0);
    auto chars=gt4::guest::read_string_pointer(m,0x1000);
    assert(chars==0x1040);
    auto rep=gt4::guest::read_string_rep(m,chars);
    assert(rep.length==3 && rep.capacity==8 && rep.references==2);
    m.write8(0x1043,'X');
    bool bad=false;
    try { (void)gt4::guest::read_string_rep(m,chars); }
    catch (const std::invalid_argument&) { bad=true; }
    assert(bad);
}
