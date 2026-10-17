#pragma once
#include "gt4/guest_ptr.hpp"

namespace gt4::guest {

template<class T> class guest_span {
public:
    guest_span(guest_ptr<T> base, std::size_t count) : base_(base), count_(count) {
        const auto room = (std::uint64_t{1} << 32) - base.address();
        if ((base.is_null() && count != 0) || count > room / wire_layout<T>::size)
            throw std::out_of_range("invalid guest array extent");
    }
    [[nodiscard]] std::size_t size() const noexcept { return count_; }
    [[nodiscard]] bool empty() const noexcept { return count_ == 0; }
    [[nodiscard]] guest_ptr<T> at(std::size_t index) const {
        if (index >= count_) throw std::out_of_range("guest array index outside extent");
        return base_.add(index);
    }
private:
    guest_ptr<T> base_;
    std::size_t count_;
};

} // namespace gt4::guest
