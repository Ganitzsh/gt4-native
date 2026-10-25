#include "types.h"

extern f32 func_003C0098(s32 a, int unused0);

f32 func_00359AD8(s32 *arg0, int unused0) {
    return func_003C0098(*arg0, unused0);
}
/* native correction of src/race/unit_0034E9A0/func_00359AD8.cpp: returns the f32 ($f0) of func_003C0098, which its caller func_00358608 reads as float. */
