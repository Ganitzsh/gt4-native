/* Host (32-bit process) replacement for the decomp's include/types.h.
 * The decomp's header declares the 128-bit EE registers with mode(TI), which a 32-bit host
 * compiler cannot emulate. They become a plain two-word struct here: enough to compile and to
 * keep sizes and alignment; the multimedia instructions that use them are still inline asm. */
#ifndef TYPES_H
#define TYPES_H

typedef signed char s8; typedef unsigned char u8; typedef short s16; typedef unsigned short u16;
typedef int s32; typedef unsigned int u32; typedef long long s64; typedef unsigned long long u64;
typedef float f32; typedef double f64;
typedef struct { u64 lo, hi; } __attribute__((aligned(16))) u128;
typedef u128 s128;

#endif
