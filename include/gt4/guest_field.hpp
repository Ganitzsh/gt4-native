#pragma once
#include "gt4/guest_ptr.hpp"

namespace gt4::guest {

// Proxy for a scalar field. Copy assignment writes a value, never rebinds the
// proxy or copies a host address into the guest object.
template<class T> class guest_field {
    static_assert(wire_scalar<T> && !std::is_const_v<T>);
public:
    guest_field(const AddressSpace& memory, guest_ptr<T> pointer)
        : memory_(&memory), writable_(nullptr), pointer_(pointer) {}
    guest_field(AddressSpace& memory, guest_ptr<T> pointer)
        : memory_(&memory), writable_(&memory), pointer_(pointer) {}
    guest_field(const guest_field&) = default;
    [[nodiscard]] operator T() const { return pointer_.load(*memory_); }
    guest_field& operator=(T value) {
        if (!writable_) throw std::runtime_error("cannot write a const guest view");
        pointer_.store(*writable_, value);
        return *this;
    }
    guest_field& operator=(const guest_field& other) { return *this = T(other); }
private:
    const AddressSpace* memory_;
    AddressSpace* writable_;
    guest_ptr<T> pointer_;
};

} // namespace gt4::guest
