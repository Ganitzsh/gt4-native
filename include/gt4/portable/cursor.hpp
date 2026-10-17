#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
namespace gt4::portable {
class Cursor { public: explicit Cursor(std::span<const std::byte> b):b_(b){} std::uint8_t read8(){ if(pos_>=b_.size()) throw std::out_of_range("cursor"); return std::to_integer<std::uint8_t>(b_[pos_++]); } std::size_t position() const { return pos_; } private: std::span<const std::byte> b_; std::size_t pos_=0; };
}
