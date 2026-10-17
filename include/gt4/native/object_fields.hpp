#pragma once
#include "gt4/guest_ptr.hpp"
#include "gt4/native_callbacks.hpp"
#include <cstdint>

namespace gt4::native::object_fields {

// Minimum wire extent needed by these accessors; not the complete game class.
// hArrayCompare is the upstream source bucket, not verified ownership of fields.
struct Layout {
    static constexpr std::size_t wire_size = 0x18C;
    static constexpr std::size_t wire_alignment = 4;
};
using Object = guest::guest_ptr<Layout>;

enum class Function : guest::Address {
    divisor = 0x001465D8,
    set_174 = 0x005C9930, set_178 = 0x005C9938, set_17c = 0x005C9940,
    set_180 = 0x005C9948, set_188 = 0x005C9950,
    get_174 = 0x005C9958, get_178 = 0x005C9960, get_184 = 0x005C9968,
    normalized = 0x005C9970,
    get_17c = 0x005C99A8, get_180 = 0x005C99B0, get_188 = 0x005C99B8,
};

void set_174(guest::AddressSpace&, Object, std::int32_t);
void set_178(guest::AddressSpace&, Object, std::int32_t);
void set_17c(guest::AddressSpace&, Object, std::int32_t);
void set_180(guest::AddressSpace&, Object, std::int32_t);
void set_188(guest::AddressSpace&, Object, std::int32_t);
std::int32_t get_174(const guest::AddressSpace&, Object);
std::int32_t get_178(const guest::AddressSpace&, Object);
std::int32_t get_17c(const guest::AddressSpace&, Object);
std::int32_t get_180(const guest::AddressSpace&, Object);
std::int32_t get_184(const guest::AddressSpace&, Object);
std::int32_t get_188(const guest::AddressSpace&, Object);
float divisor(const guest::AddressSpace&, Object);
float normalized(const guest::AddressSpace&, Object);

// Register the thirteen original addresses. Float results use IEEE-754 bits in
// the registry's uint32 result slot; this is a native adapter ABI, not PS2 f0.
// The memory and registry must outlive every registered invocation.
void bind_callbacks(guest::NativeCallbacks&, guest::AddressSpace&);

} // namespace gt4::native::object_fields
