#include "types.h"
void *memcpy(void *, const void *, unsigned int);

s32 func_00538C68(s32 *, int);                            /* extern */

s32 func_00531738(s32 *arg0, int arg1) {
    if (*arg0 != 0) {
        return func_00538C68(arg0, arg1);
    }
    return 0;
}
/* native correction of src/network/unit_00494578/func_00531738.c: returns func_00538C68's result, or 0 (the *arg0 just tested) when it is not called */
