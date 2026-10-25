#include "types.h"

extern "C" s32 func_003CC840(char *arg0);
extern "C" void func_003CBB28(char *, s32, char *, s32, f32);

extern "C" void func_003CF570(char *arg0, s32 arg1, f32 t) {
    func_003CBB28(arg0 + 4, func_003CC840(arg0), arg0, arg1, t);
}
/* native correction of src/race/unit_003CCAE0/func_003CF570.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_003CBB28 */
