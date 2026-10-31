/* Included before every game source by tools/game_cc.py (-include): what ee-gcc 2.96 provides
 * built in and the i386 gcc 2.9 (2.9-ee-990721 retargeted, see docs/GAME_TOOLCHAIN.md) lacks. */
#ifndef GT4_PRELUDE_H
#define GT4_PRELUDE_H

/* The EE's 128-bit integers (`int __attribute__((mode(TI)))`): 16 bytes, 16-aligned.  game_cc.py
 * rewrites each mode(TI) typedef of a source into one of these, so the names stay distinct types
 * (s128 and u128 overload differently) and a repeated typedef names the same type. */
#ifdef __cplusplus
/* Copies to and from volatile 128-bit objects (hardware FIFOs, volatile buffers): a member template
 * is never a copy assignment operator, so the structures stay trivial (union members, aggregates)
 * and a plain copy still takes the implicit operator.  Assigning an integer (`q = 0`, as the EE's
 * TImode allows) extends it by its signedness; these are not copy assignment operators either. */
#define __GT4_INT_SET(T, HI) \
  void operator= (T v) { lo = (unsigned long long) v; hi = (HI); } \
  void operator= (T v) volatile { lo = (unsigned long long) v; hi = (HI); }
#define __GT4_SIGNED_HI ((unsigned long long) ((long long) v >> 63))
#define __GT4_VOLATILE_COPY \
  template <class __gt4_T> void operator= (const volatile __gt4_T &o) volatile { lo = o.lo; hi = o.hi; } \
  __GT4_INT_SET (int, __GT4_SIGNED_HI) __GT4_INT_SET (unsigned int, 0) \
  __GT4_INT_SET (long, __GT4_SIGNED_HI) __GT4_INT_SET (unsigned long, 0) \
  __GT4_INT_SET (long long, __GT4_SIGNED_HI) __GT4_INT_SET (unsigned long long, 0)
#else
#define __GT4_VOLATILE_COPY
#endif
struct __gt4_u128 { unsigned long long lo, hi; __GT4_VOLATILE_COPY } __attribute__((aligned(16)));
struct __gt4_s128 { unsigned long long lo, hi; __GT4_VOLATILE_COPY } __attribute__((aligned(16)));
static __inline__ struct __gt4_u128 __gt4_zero_u128 (void) { struct __gt4_u128 z; z.lo = 0; z.hi = 0; return z; }
static __inline__ struct __gt4_s128 __gt4_zero_s128 (void) { struct __gt4_s128 z; z.lo = 0; z.hi = 0; return z; }

/* gcc 2.96's varargs builtins (its stdarg.h, which the decomp's sources inlined), over the i386
 * stack: every argument is pushed in 4-byte slots, 64-bit long and double in two. */
typedef char *__builtin_va_list;
#define __gt4_va_size(T) ((sizeof (T) + sizeof (int) - 1) & ~(sizeof (int) - 1))
#define __builtin_stdarg_start(ap, last) ((void)((ap) = (char *)__builtin_next_arg (last)))
#define __builtin_va_start(ap, last) __builtin_stdarg_start (ap, last)
#define __builtin_varargs_start(ap) ((void)((ap) = (char *)__builtin_next_arg ()))
#define __builtin_va_arg(ap, T) (*(T *)(((ap) += __gt4_va_size (T)) - __gt4_va_size (T)))
#define __builtin_va_end(ap) ((void)0)
#define __builtin_va_copy(d, s) ((void)((d) = (s)))

/* ee-gcc predefines __MIPSEL__ (the EE is little-endian MIPS); newlib sources choose their IEEE
 * word order by it (little-endian, as on i386). */
#define __MIPSEL__ 1

/* Branch hints (gcc 2.96): no effect on the result. */
#define __builtin_expect(e, c) (e)

/* Targets of the one-instruction MIPS asm statements that tools/game_cc.py rewrites into C
 * (translate_asm): the FPU and MMI instructions as x87 / C code, the EE kernel's privileged ones
 * as calls the native runtime provides. */
#ifdef __cplusplus
extern "C" {
#endif
void __gt4_ee_ei (void);                 /* ei: enable interrupts */
void __gt4_ee_di (void);                 /* di: disable interrupts */
unsigned int __gt4_ee_mfc0 (int reg);    /* mfc0 rt, $reg (Status = 12, TagLo = 28, ...) */
void *__gt4_ee_get_gp (void);            /* the module's $gp (GetGp) */
void __gt4_ee_set_gp (void *gp);         /* SetGp */
void __gt4_ee_break (int code);          /* break: the debugger trap */
/* The same, as plain C for native sources (game_src/) written without asm (runtime/eeops.cpp):
 * - gt4_interrupts_enabled: Status.EIE as game code reads it (mfc0 $12 & 0x10000): whether the
 *   calling thread has interrupts enabled; also 1 inside an interrupt handler, where the EE masks
 *   them with EXL instead (so "if enabled: disable, ..., enable" runs its di/ei there too);
 * - gt4_in_interrupt: inside an interrupt handler (Status.EXL);
 * - gt4_interrupts_disable / gt4_interrupts_enable: di / ei;
 * - gt4_cycle_count: the Count register (mfc0 $9), the CPU clock (twice the bus clock);
 * - gt4_get_gp / gt4_set_gp: the module's $gp ($28). */
int gt4_interrupts_enabled (void);
int gt4_in_interrupt (void);
void gt4_interrupts_disable (void);
void gt4_interrupts_enable (void);
unsigned int gt4_cycle_count (void);
void *gt4_get_gp (void);
void gt4_set_gp (void *gp);
/* - gt4_timer_count: what a load of EE timer n's COUNT (T0..T3_COUNT, 0x10000000 + 0x800 n) reads: the ticks
 *   of the clock its Tn_MODE selects, from the runtime's clock (runtime/eeops.cpp);
 * - gt4_not_native: stops the game with a report naming fn, its caller (pass __builtin_return_address (0)) and
 *   why; for the game_src/ stubs of hardware paths the native program never runs (runtime/missing.cpp). */
unsigned int gt4_timer_count (int timer);
void gt4_not_native (unsigned int fn, const void *caller, const char *why) __attribute__ ((noreturn));
/* The runtime's frame clock (runtime/kernel.cpp), in place of the PS2's VBlank interrupt: gt4_frame_tick_add
 * has fn (arg) called once a frame, at the game's frame rate (59.94 Hz), with no game thread between two
 * instructions of its own (as an interrupt handler ran: a thread that disabled interrupts, di, holds it back),
 * before the callbacks added earlier when `first` is set, after them otherwise. Returns an id for
 * gt4_frame_tick_remove. */
int gt4_frame_tick_add (void (*fn) (unsigned int arg), unsigned int arg, int first);
void gt4_frame_tick_remove (int id);
#ifdef __cplusplus
}
#endif
/* Status.IE (mfc0 $12 & 1), which libstream's lock, cache and thread helpers test to tell an interrupt
 * handler (the EE kernel runs handlers with IE clear) and then take the kernel's i-variants: clear inside
 * the runtime's frame callbacks, completions and alarms, as runtime/eeops.cpp's mfc0 reads it. (Until
 * 2026-10-31 it read 1 everywhere: a frame callback that found its semaphore gone then slept in
 * DelayThread holding the kernel's lock.) */
static __inline__ int gt4_status_ie (void) { return !gt4_in_interrupt (); }

/* min.s/max.s compare as the EE FPU does, as sign-magnitude integers: it has no NaN or infinity, so
 * 0x7FFFFFFF is its largest value, not a NaN. */
static __inline__ int __gt4_fpu_order (float f) { union { float f; int i; } u; u.f = f; return u.i ^ ((u.i >> 31) & 0x7FFFFFFF); }
static __inline__ float __gt4_minf (float a, float b) { return __gt4_fpu_order (a) < __gt4_fpu_order (b) ? a : b; }
static __inline__ float __gt4_maxf (float a, float b) { return __gt4_fpu_order (a) > __gt4_fpu_order (b) ? a : b; }
static __inline__ float __gt4_sqrtf (float x) { float r; __asm__ ("fsqrt" : "=t" (r) : "0" (x)); return r; }
static __inline__ float __gt4_absf (float x) { float r; __asm__ ("fabs" : "=t" (r) : "0" (x)); return r; }
static __inline__ float __gt4_bits_float (unsigned int u) { union { unsigned int u; float f; } v; v.u = u; return v.f; }
static __inline__ unsigned int __gt4_float_bits (float f) { union { unsigned int u; float f; } v; v.f = f; return v.u; }
/* plzcw (low word): how many bits after bit 31 equal it. */
static __inline__ int __gt4_plzcw (int x)
{
  unsigned int u = x < 0 ? ~(unsigned int) x : (unsigned int) x;
  int n = 0;
  while (n < 31 && !(u & (0x40000000u >> n)))
    n++;
  return n;
}
/* The low 32 bits of any value (an int, a 64-bit long, a 128-bit register struct). */
#define __GT4_LO32(e) ({ __typeof__ (e) __gt4_t = (e); *(unsigned int *) &__gt4_t; })

/* libgcc's longlong.h (the image's __muldi3 and __udivmoddi4 copies) defines umul_ppmm only for the MIPS
 * (multu, mflo, mfhi): the 32 x 32 -> 64-bit product here, in C. Written with libgcc2's own UDItype and USItype,
 * since libgcc2.c defines int, long and unsigned away (bogus_type). */
#define umul_ppmm(w1, w0, u, v) \
  do { UDItype __gt4_p = (UDItype) (USItype) (u) * (USItype) (v); \
       (w1) = (USItype) (__gt4_p >> 32); (w0) = (USItype) __gt4_p; } while (0)

/* The other asm statements the driver translates (multi-instruction, MMI, FPU, COP2, VU0 macro
 * mode...): tools/game_toolchain/asm_translate.py. */
#include "gt4_asm.h"

#endif
