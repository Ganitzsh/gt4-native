#include "types.h"

struct S00574D78 {
    char pad[0x28];
    s32 unk28;
    s32 unk2C;
};

extern "C" int func_005763B8(S00574D78 *arg0);

extern "C" int func_00574D78(S00574D78 *arg0, int unused0) {
    int r = func_005763B8(arg0);
    arg0->unk28 = 0;
    arg0->unk2C = 0;
    return r;
}
/* native correction of src/libstream/unit_005676B0/func_00574D78.cpp: returns func_005763B8's value (-1, corrected in game_src/func_005763B8.c) */
