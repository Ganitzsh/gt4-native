#pragma once
// The EE kernel on the host: threads, semaphores, sleep/wakeup, interrupt handlers (INTC, DMAC),
// alarms, and the 60 Hz vertical blank.
//
// Scheduling follows the EE kernel: one guest thread runs at a time; the highest-priority ready thread
// runs (0 is highest); a thread of equal priority runs until it blocks or rotates the ready queue.
// Each guest thread is a host thread; the scheduler hands the CPU over with per-thread events.
//
// Interrupts are delivered as on the PS2: the interrupt thread suspends the running guest thread at
// whatever instruction it is on, runs the handlers (on the interrupt thread, with the guest thread
// frozen), then resumes it, or preempts it if a handler made a higher-priority thread ready. While a
// thread has interrupts disabled (DIntr) or is inside runtime code (KernelScope), interrupts are held
// and delivered when it leaves.
#include <cstdint>

namespace gt4::rt {

void kernel_init();                  // makes the calling host thread the guest main thread
void kernel_start_interrupts();      // starts the vblank/alarm clock
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

// INTC causes (EE): GS 0, SBUS 1, VBLANK_START 2, VBLANK_END 3, VIF0 4, VIF1 5, VU0 6, VU1 7,
// IPU 8, TIMER0..3 9-12, SFIFO 13, VU0WD 14. DMAC causes are the channel numbers 0-9 (plus 15).
void kernel_raise_intc(int cause);   // from host services (e.g. GS FINISH, timer)
void kernel_post_callback(void (*fn)(uint32_t), uint32_t arg);  // runs fn(arg) in interrupt context
// Called on the interrupt thread at each VBlank start, before the INTC 2 handlers run.
void kernel_set_vblank_hook(void (*fn)(uint32_t vblank_count));
void kernel_raise_dmac(int channel);

// Ticks of the PS2 bus clock (147.456 MHz) since boot, from the host's monotonic clock.
uint64_t kernel_bus_cycles();
uint32_t kernel_vblank_count();

}  // namespace gt4::rt
