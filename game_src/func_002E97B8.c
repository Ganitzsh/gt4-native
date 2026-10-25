#include "types.h"
void *memcpy(void *, const void *, unsigned int);

f32 mWidget__getWindowW(struct func_002E97B8_arg0 *arg0);                            /* extern */
f32 mWidget__getWindowH(struct func_002E97B8_arg0 *arg0);                            /* extern */

struct func_002E97B8_arg0 {
    char pad0[0xC4];
    s32 unkC4;
};

f32 func_002E97B8(struct func_002E97B8_arg0 *arg0) {
    if (arg0->unkC4 != 0) {
        return mWidget__getWindowH(arg0);
    }
    return mWidget__getWindowW(arg0);
}
/* native correction of src/ui/unit_002E93C0/func_002E97B8.c: returns the f32 of the tail call mWidget__getWindowH / mWidget__getWindowW that the caller reads from $f0 (their declarations made f32). */
