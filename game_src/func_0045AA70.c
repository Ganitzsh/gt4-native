#include "types.h"
#define NULL 0
void *memcpy(void *, const void *, unsigned int);
void *func_0045A718(void *, void *, s32, f32);

void *func_0045AA70(void *arg0, void *arg1, f32 t) {
    func_0045A718(arg0, arg1, 0x31, t);
    return arg0;
}
/* native correction of src/engine/unit_00457510/func_0045AA70.c: takes the blend factor t (passed through in $f12 on the PS2) and passes it on to func_0045A718 */
