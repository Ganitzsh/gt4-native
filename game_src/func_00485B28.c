/* Native copy of 00485B28 (matched; corrected for the native calling convention): under the table's lock, finds
 * the entry for a 64-bit key (004859A0) and calls fn with the entry's 64-bit value.
 * The matched source takes the key as s32: on the EE the whole 64-bit $a1 (004857C8 passes 00485BA0's 64-bit
 * name hash) reaches 004859A0 unchanged; natively only the low word was passed, so no entry ever matched. */
#include "types.h"
void *memcpy(void *, const void *, unsigned int);
#include "m2c_macros.h"

s32 func_004859A0(void *, u64);                     /* extern */
void func_00576788(s32);                            /* extern */
void func_005767C0(void *);                      /* extern */

struct func_00485B28_arg0 {
    char pad0[0x30];
    s32 unk30;
};

s32 func_00485B28(struct func_00485B28_arg0 *arg0, u64 arg1, s32 (*arg2)(s64)) {
    s32 temp_v0;
    s32 var_s0;

    func_00576788(arg0);
    var_s0 = 0;
    temp_v0 = func_004859A0(arg0, arg1);
    if (temp_v0 >= 0) {
        var_s0 = 1;
        arg2(M2C_FIELD(((temp_v0 * 0x10) + arg0->unk30), s64 *, 8));
    }
    func_005767C0(arg0);
    return var_s0;
}
