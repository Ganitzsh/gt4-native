#include "types.h"

struct Blob {
    char d[0x100];
};

struct Work {
    char pad0[0x26];
    struct Blob blob;
};

struct Sess {
    char pad0[0x5A8];
    struct Work *work;
    char pad5AC[0x60C - 0x5AC];
    struct Blob blob60C;
};

s32 func_004F0C88(void *arg0);
s32 func_004F0CD0(void *, s32);
s32 func_005A48D8(void *, s32, s32);
s32 func_005A6AB0(void *, void *, s32);

s32 func_004F37C0(void *arg0, struct Blob *arg1)
{
    if (func_004F0C88(arg0) != 0) {
        struct Work *w;
        func_005A48D8(((struct Sess *)arg0)->work, 0, 0x126);
        w = ((struct Sess *)arg0)->work;
        func_005A6AB0((char *)w + 0x15, (char *)arg0 + 0xAF8, 0x11);
        w->blob = *arg1;
        ((struct Sess *)arg0)->blob60C = *arg1;
        return func_004F0CD0(arg0, 0x24);
    }
    return 0;
}
/* native correction of src/network/unit_00494578/func_004F37C0.c: returns func_004F0CD0's result (1) after the request is queued and 0 when func_004F0C88 fails, the values $v0 holds at each return */
