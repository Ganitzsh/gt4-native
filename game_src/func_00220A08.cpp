/* If the counter at +0x8 is still positive does nothing; otherwise calls func_001062B8 on the global D_006184D0 (purpose unknown). */
#include "types.h"

struct Obj_00220A08 {
    char pad[8];
    s32 unk8;
};

extern "C" s32 func_001062B8(void *obj);
extern "C" char D_006184D0[];

extern "C" s32 func_00220A08(Obj_00220A08 *obj) {
    if (obj->unk8 > 0) {
        return obj->unk8;
    }
    return func_001062B8(D_006184D0);
}
/* native correction of src/ui/unit_0021FAD0/func_00220A08.cpp: returns s32: obj->unk8 (loaded into $v0 for the test) when positive, else func_001062B8's result */
