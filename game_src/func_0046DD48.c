#include "types.h"
void *memcpy(void *, const void *, unsigned int);

s32 func_0046D770(void *);                          /* extern */
void func_0046E920(void *, s32);                 /* extern */
s32 func_0046EA08(void *);                          /* extern */

struct func_0046DD48_temp_s1 {
    char pad0[0x10];
    s32 unk10;
};

struct func_0046DD48_arg0 {
    char pad0[0xAD4];
    s32 unkAD4;
    s32 unkAD8;
};

s32 func_0046DD48(void *arg0) {
    s32 temp_s2;
    struct func_0046DD48_temp_s1 *temp_s1;

    temp_s1 = arg0 + 0xA88;
    temp_s2 = func_0046EA08(temp_s1) - 2;
    if (func_0046D770(arg0) != 0) {
        ((struct func_0046DD48_arg0 *)arg0)->unkAD8 = temp_s2;
        ((struct func_0046DD48_arg0 *)arg0)->unkAD4 = (s32) temp_s1->unk10;
        func_0046E920(temp_s1, temp_s2);
        return func_0046D770(arg0);
    }
    return 0;
}
/* native correction of src/engine/unit_0046A050/func_0046DD48.c: returns the second func_0046D770 call's result, or 0 (the first one's) when it fails, as $v0 held on the PS2 */
