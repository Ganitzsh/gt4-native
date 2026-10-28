/* compiler: ee-gcc2.96 */
/* Native code for 0042F408 (matched; corrected for the native calling convention): builds a tagged value
 * (tag 7, a 64-bit database id from 00440B18) and hands it to 0042F0E0. 00440B18 returns 00447CF8's id,
 * 00443E00's (kind << 32) | row: a real 64-bit value. The matched source declares 00440B18 as returning s32;
 * on the EE the whole $v0 is stored by the sd at 0x42F434. Natively the high word (the kind) was dropped;
 * the declaration now returns s64. */
#include "types.h"
struct V { char tag; s64 val; };
extern "C" s64 func_00440B18(s32 a);
extern "C" void func_0042F0E0(void *self, V *v);
extern "C" void func_0042F408(void *self, s32 x) {
    V v;
    v.tag = 7;
    v.val = func_00440B18(x);
    func_0042F0E0(self, &v);
}
