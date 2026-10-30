#pragma once
// runtime/guest.h for eeref: the guest address space is eeref's own arrays (RAM, VU memory, its stack), not
// the process's, so code compiled into it that uses guest addresses (vu0_interp.cpp) maps them through eeref.
#include <cstdint>

namespace gt4::rt {

void *guest_ptr(uint32_t addr);

}  // namespace gt4::rt
