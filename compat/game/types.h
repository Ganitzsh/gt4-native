/* Native (i386, tools/game_cc.py) replacement for the decomp's include/types.h; it comes first on
 * the include path.  The EE's 128-bit registers (mode(TI) in the decomp) have no integer mode in the
 * 32-bit i386 compiler: they are the 16-byte, 16-aligned structures of gt4_prelude.h, so sizes,
 * offsets and alignments stay the EE's.  Copies work; arithmetic on them does not compile.  long is
 * 64-bit in this compiler, as on the EE. */
#ifndef TYPES_H
#define TYPES_H

typedef signed char s8; typedef unsigned char u8; typedef short s16; typedef unsigned short u16;
typedef int s32; typedef unsigned int u32; typedef long long s64; typedef unsigned long long u64;
typedef float f32; typedef double f64;
typedef struct __gt4_s128 s128; typedef struct __gt4_u128 u128;

#endif
