/* Included before every game source by tools/game_cc.py (-include): what ee-gcc 2.96 provides
 * built in and the i386 gcc 2.9 (2.9-ee-990721 retargeted, see docs/GAME_TOOLCHAIN.md) lacks. */
#ifndef GT4_PRELUDE_H
#define GT4_PRELUDE_H

/* The EE's 128-bit integers (`int __attribute__((mode(TI)))`): 16 bytes, 16-aligned.  game_cc.py
 * rewrites each mode(TI) typedef of a source into one of these, so the names stay distinct types
 * (s128 and u128 overload differently) and a repeated typedef names the same type. */
struct __gt4_u128 { unsigned long long lo, hi; } __attribute__((aligned(16)));
struct __gt4_s128 { unsigned long long lo, hi; } __attribute__((aligned(16)));

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

#endif
