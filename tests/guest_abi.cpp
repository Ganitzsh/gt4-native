#include "gt4/guest_abi.hpp"
#include <cassert>
int main() {
    using namespace gt4::guest;
    constexpr VtableEntry entry{-12, 3, 0x00301234u};
    static_assert(entry.this_delta == -12);
    static_assert(entry.target == 0x00301234u);
    assert(sizeof(Address) == 4);
    assert(sizeof(ObjectLayout) == 16);
}