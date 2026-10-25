#include "types.h"

struct func_003FA238_arg0 {
    char pad0[0x1003E];
    s16 unk1003E;
};

extern "C" s32 func_003FA238(struct func_003FA238_arg0 *arg0) {
    arg0->unk1003E = 0;
    return 0x157529FF;
}
/* native correction of src/race/unit_003F7150/func_003FA238.cpp: returns 0x157529FF: it never writes $v0, and its only caller (DynamicsConductorLicense__virtual_27, which returns it) leaves that constant there. */
