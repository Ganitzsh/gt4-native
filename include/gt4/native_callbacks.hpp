#pragma once
#include "gt4/guest_dispatch.hpp"
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <unordered_map>

namespace gt4::guest {

// A transitional bridge: map a guest function address to a host implementation.
// It does not execute guest MIPS instructions or implement the PS2 calling ABI.
class NativeCallbacks {
public:
    using Callback = std::function<std::uint32_t(Address adjusted_this, std::uint32_t argument)>;

    void bind(Address guest_function, Callback callback) {
        if (guest_function == 0 || !callback)
            throw std::invalid_argument("invalid guest callback binding");
        if (!callbacks_.emplace(guest_function, std::move(callback)).second)
            throw std::invalid_argument("duplicate guest callback binding");
    }

    [[nodiscard]] bool contains(Address guest_function) const noexcept {
        return callbacks_.find(guest_function) != callbacks_.end();
    }

    [[nodiscard]] std::uint32_t call(Address guest_function,
                                     Address adjusted_this,
                                     std::uint32_t argument) const {
        auto it = callbacks_.find(guest_function);
        if (it == callbacks_.end())
            throw std::out_of_range("guest callback has no native binding");
        return it->second(adjusted_this, argument);
    }

    [[nodiscard]] std::uint32_t call_virtual(const MemoryView& memory,
                                              Address object,
                                              std::uint32_t method_index,
                                              std::uint32_t argument) const {
        auto dispatch = resolve_virtual_call(memory, object, method_index);
        if (dispatch.index != 0)
            throw std::runtime_error("nonzero vtable index is not supported yet");
        return call(dispatch.target, dispatch.adjusted_this, argument);
    }

private:
    std::unordered_map<Address, Callback> callbacks_;
};

} // namespace gt4::guest
