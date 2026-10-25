#include "types.h"

extern "C" void *func_005A48D8(void *arg0, s32 arg1, s32 arg2);

extern "C" void * func_0044FFA0(void *arg0) {
    return func_005A48D8(arg0, 0, 0x10);
}
/* native correction of src/engine/unit_0044FFA0/func_0044FFA0.cpp (gt4-decomp tools/argcheck.py fix --kind novalue --native): novalue func_0044FFA0 */
