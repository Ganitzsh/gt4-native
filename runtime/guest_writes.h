#pragma once
// Writes to the guest's RAM that the code reading it does not make itself (PLAN.md D5's profile, (1)):
// runtime/frame.cpp's hash memo keeps a content's hash for the rest of a frame walk only while this count has not
// moved. Every such writer calls guest_written() before the bytes change, so a lookup that starts once a write has
// begun misses:
//   - kernel.cpp: dispatch (any guest thread given the CPU) and deliver_pending (the frame's callbacks, completions and
//     alarms, in either delivery): guest code that may run while a walk is under way (a link_send's KernelScope exit
//     can deliver, and a handler can make another thread run).
//   - frame.cpp: the walk's synchronous GS reads, copied into guest memory after their block.
//   - link.cpp: link_poll's GS image into a guest destination (a read waited for), and link_async_deliver.
//   - iop.cpp: clock_pusher, a host thread writing the IOP clock into guest RAM every 100 ms.
//   - record.cpp: GT4_POKE's writes (debugging).
// Not writers here, checked: disc reads into EE RAM (PCDV, STRP) and every SIF RPC's writes run on the calling guest
// thread inside its syscall (sceSifCallRpc runs the handler before it returns, asynchronous calls included; only their
// completion callback is posted), so the kernel's points above cover any of them made while a walk waits; the audio
// thread writes runtime buffers and SPU RAM only; link_poll's other records land in runtime storage (pads, the
// settings ring, options); the snapshot thread only reads. Every host thread the runtime creates was read for
// this (2026-11-10, lanes C and D): crash.cpp's watch thread and watch.cpp's arm_all only set debug registers;
// heap_log's thread and profile.cpp's sampler only read; hw_audit's writer only logs; record.cpp's snapshot only
// reads, its poke is listed above; iop.cpp's clock_pusher is listed above; the interrupt thread delivers through
// deliver_pending and the guest threads run through dispatch, both listed. A new writer of guest RAM outside the
// reading code's own flow must call guest_written() first.
#include <atomic>
#include <cstdint>

namespace gt4::rt {

inline std::atomic<uint32_t> g_guest_writes{0};
inline void guest_written() { g_guest_writes.fetch_add(1); }

}  // namespace gt4::rt
