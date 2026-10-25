#include "types.h"

extern "C" void func_005792E8(s32 *arg0, s32 arg1);
extern "C" f32 mBlockTransition__getBlock(f32, f32, s32 *);

struct func_003CCB60_arg0 {
    char pad0[0x96D0];
    s32 unk96D0;
};

extern "C" f32 func_003CCB60(void *arg0, f32 fparg0, f32 fparg1) {
    s32 sp0;
    f32 r;
    struct func_003CCB60_arg0 *self = (struct func_003CCB60_arg0 *)arg0;

    func_005792E8(&sp0, self->unk96D0);
    r = mBlockTransition__getBlock(fparg0, fparg1, &sp0);
    self->unk96D0 = sp0;
    return r;
}
/* native correction of src/race/unit_003CCAE0/func_003CCB60.cpp: returns the f32 ($f0) of mBlockTransition__getBlock, the random value the callers read. */
