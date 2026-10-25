#include "types.h"
#define NULL 0
void *memcpy(void *, const void *, unsigned int);

s32 func_00208308(s32 arg0);
s32 func_0025C418(s32);
s32 func_00208470(s32 arg0, s32 arg1) {
    s32 p;
    s32 i;
    p = func_00208308(arg0);
    for (i = 0; p != 0 && i != arg1; i++) {
        p = func_0025C418(p);
    }
    return p;
}
/* native correction of src/ui/mPhotoMapWindow/func_00208470.c: returns s32 p, the last node pointer left in $v0 on every exit path on the PS2 */
