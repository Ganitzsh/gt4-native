#pragma once
// The EE kernel on the host: threads, semaphores, sleep/wakeup, alarms, and the frame clock (the game's
// 60 Hz frame, in place of the PS2's VBlank interrupt; no interrupt or DMA controller).
//
// Scheduling follows the EE kernel: one guest thread runs at a time; the highest-priority ready thread
// runs (0 is highest); a thread of equal priority runs until it blocks or rotates the ready queue.
// Each guest thread is a host thread; the scheduler hands the CPU over with per-thread events.
//
// The frame's callbacks, completions and alarms run as interrupt handlers did on the PS2: the clock's thread
// suspends the running guest thread at whatever instruction it is on, runs them (with the guest thread
// frozen), then resumes it, or preempts it if one made a higher-priority thread ready. While a thread has
// interrupts disabled (DIntr) or is inside runtime code (KernelScope), they are held and run when it leaves.
#include <cstdint>

namespace gt4::rt {

void kernel_init();                  // makes the calling host thread the guest main thread
void kernel_start_interrupts();      // starts the frame and alarm clock
void kernel_scan_syscalls();         // finds the syscall stubs (call before install_trampolines)
void kernel_install_syscalls();      // points every syscall stub of the image at its implementation

// Marks runtime code: no interrupt is delivered to this thread inside it (nesting allowed).
struct KernelScope {
    KernelScope();
    ~KernelScope();
    KernelScope(const KernelScope &) = delete;
    KernelScope &operator=(const KernelScope &) = delete;
};

// Interrupt control (the SDK's DIntr/EIntr): returns whether interrupts were enabled before.
int kernel_di();
int kernel_ei();
bool kernel_in_interrupt();
bool kernel_interrupts_enabled();    // for the calling guest thread (Status.EIE)

void kernel_post_callback(void (*fn)(uint32_t), uint32_t arg);  // runs fn(arg) as the frame's callbacks run
// Called on the frame clock's thread at each frame, before the game's frame callbacks (gt4_frame_tick_add).
void kernel_set_frame_hook(void (*fn)(uint32_t frame_count));
// Called after every delivery of the frame's callbacks, completions and alarms, in the same context.
void kernel_set_interrupt_hook(void (*fn)());

// Ticks of the PS2 bus clock (147.456 MHz) since boot, from the host's monotonic clock.
uint64_t kernel_bus_cycles();
void kernel_set_paused(bool paused);  // the host's settings menu: stops the bus clock (frames, alarms)
bool kernel_paused();
uint32_t kernel_frame_count();
// Takes time the game did not run (a debugging tool's own cost) off the bus clock; safe in an
// exception handler. Once it has been used, the clock is kept from going back.
void kernel_add_stall(int64_t qpc_ticks);

}  // namespace gt4::rt
