#include "types.h"
void *memcpy(void *, const void *, unsigned int);

s32 func_004EAE50(struct func_004EB770_arg0 *arg0);                            /* extern */
s32 func_004EB4E0(struct func_004EB770_arg0 *arg0);                            /* extern */

struct func_004EB770_arg0 {
    char pad0[0xD0];
    s32 unkD0;
};

s32 func_004EB770(struct func_004EB770_arg0 *arg0) {
    if (arg0->unkD0 == 0) {
        return func_004EAE50(arg0);
    }
    return func_004EB4E0(arg0);
}
/* native correction of src/network/unit_00494578/func_004EB770.c: returns the value of the tail call func_004EAE50 / func_004EB4E0 */
