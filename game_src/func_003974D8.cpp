#include "types.h"

void func_00397A00(s32, s32, f32);              /* extern */
void func_00397A20(s32, f32, s32);              /* extern */

struct func_003974D8_arg0 {
    char pad0[0x4];
    s32 unk4;
    char pad8[0xC];
    s32 unk14;
    char pad18[0xC];
    s32 unk24;
    char pad28[0x2C];
    s32 unk54;
    s32 unk58;
    char pad5C[0x1C];
    s32 unk78;
};

void func_003974D8(struct func_003974D8_arg0 *arg0, s32 arg1, f32 fparg0) {
    func_00397A00(arg0->unk4, arg1, fparg0);
    func_00397A00(arg0->unk54, arg1, fparg0);
    func_00397A00(arg0->unk58, arg1, fparg0);
    func_00397A00(arg0->unk14, arg1, fparg0);
    func_00397A00(arg0->unk24, arg1, fparg0);
    func_00397A20(arg0->unk78, fparg0, arg1);
}
/* native correction of src/race/unit_00394D48/func_003974D8.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_00397A00 */
