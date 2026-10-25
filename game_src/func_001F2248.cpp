#include "types.h"

struct Str {
    char *p;
};

extern char D_006959D8[];
extern char D_00645570[];

extern "C" int func_001F1368(s32 arg0);
extern "C" void mUpdateContext__Sync(s32 arg0);
extern "C" s32 func_004F3128(void *arg0, const char *arg1, const char *arg2);

static inline const char *c_str(Str *s) {
    s32 len = *(s32 *)(s->p - 0x10);
    if (len == 0) {
        return D_006959D8;
    }
    s->p[len] = 0;
    return s->p;
}

extern "C" int func_001F2248(s32 arg0, Str *a, Str *b) {
    while (func_004F3128(D_00645570, c_str(a), c_str(b)) == 0) {
        mUpdateContext__Sync(1);
    }
    return func_001F1368(arg0);
}
/* native correction of src/online/unit_001DC8F0/func_001F2248.cpp: returns int, the result of the tail call to func_001F1368 (func_001DEF80 tests it) */
