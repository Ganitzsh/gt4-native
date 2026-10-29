#pragma once
// runtime/guest.h for eeref: the guest address space is eeref's own arrays (RAM, VU memory, its stack), not
// the process's, so runtime code compiled into it (runtime/native/vu0.cpp) maps addresses through eeref.
#include <cstdint>

namespace gt4::rt {

void *guest_ptr(uint32_t addr);

}  // namespace gt4::rt
