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
 * TImode allows) sign-extends it; these are not copy assignment operators either. */
#define __GT4_INT_SET(T) \
  void operator= (T v) { lo = (unsigned long long) v; hi = (long long) v < 0 && (T) -1 < 0 ? ~0ull : 0; } \
  void operator= (T v) volatile { lo = (unsigned long long) v; hi = (long long) v < 0 && (T) -1 < 0 ? ~0ull : 0; }
#define __GT4_VOLATILE_COPY \
  template <class __gt4_T> void operator= (const volatile __gt4_T &o) volatile { lo = o.lo; hi = o.hi; } \
  __GT4_INT_SET (int) __GT4_INT_SET (unsigned int) __GT4_INT_SET (long) __GT4_INT_SET (unsigned long) \
  __GT4_INT_SET (long long) __GT4_INT_SET (unsigned long long)
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
#ifdef __cplusplus
}
#endif

static __inline__ float __gt4_minf (float a, float b) { return a < b ? a : b; }
static __inline__ float __gt4_maxf (float a, float b) { return a > b ? a : b; }
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

/* The other asm statements the driver translates (multi-instruction, MMI, FPU, COP2, VU0 macro
 * mode...): tools/game_toolchain/asm_translate.py. */
#include "gt4_asm.h"

#endif
