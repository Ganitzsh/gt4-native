#include "types.h"

extern void *D_00689F50;
extern "C" void func_0057DD98(void *);

struct func_0057DC60_arg0 {
    void *unk0;
    void *unk4;
};

extern "C" void *func_0057DC60(struct func_0057DC60_arg0 *arg0, s32 arg1) {
    void *r0 = (void *)(arg1); /* PS2: $v0 at entry, untouched by func_0057DD98; its only caller func_0057DD28 has it equal to arg1 (and discards the result) */
    func_0057DD98(arg0);
    arg0->unk0 = &D_00689F50;
    arg0->unk4 = (void *)(arg1);
    return r0;
}
/* native correction of src/libstream/unit_0057A128/func_0057DC60.cpp: returns arg1, the value $v0 holds on the PS2 when called from func_0057DD28 (func_0057DD98 never writes $v0), and calls func_0057DD98 as void */
