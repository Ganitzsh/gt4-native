#include "types.h"

extern int func_00575DA0(s32, s32 a1);

int func_00575F90(s32 a0, s32 a1)
{
    return func_00575DA0(a1, a1);
}
/* native correction of src/libstream/unit_005676B0/func_00575F90.cpp: returns func_00575DA0's value (free: an explicit 0, see game_src/func_00575DA0.cpp) */
