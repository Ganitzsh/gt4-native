/* compiler: ee-gcc2.96-nosib */
/* Native copy of 004857C8 (matched; corrected for the native calling convention): looks the name arg0 up in the
 * table D_00624898 by its 64-bit hash (00485BA0) and calls D_00463560 with the entry (00485B28).
 * The matched source declares the hash and 00485B28's key as s32: the EE keeps the 64 bits in $v0 / $a1;
 * natively only the low word was passed (see game_src/func_00485B28.c). */
#include "types.h"
#define NULL 0
void *memcpy(void *, const void *, unsigned int);

s32 func_00485B28(s32, u64, s32);       /* extern */
u64 func_00485BA0(s32 arg0);                                /* extern */

extern char D_00463560[];
extern char D_00624898[];
void func_004857C8(s32 arg0, s32 arg1, s32 arg2, s32 arg3, s32 arg4, s32 arg5, s32 arg6) {
    func_00485B28((s32)D_00624898, func_00485BA0(arg0), (s32)D_00463560);
}
