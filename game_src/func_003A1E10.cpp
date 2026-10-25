#include "types.h"

extern "C" void func_0057DA20(void *dst, const char *fmt, s32 arg);
extern "C" s32 func_0048F628(void *globalObj, const char *path, s32 *outObj, s32 flag);

extern "C" char D_00621518[];

extern "C" s32 func_003A1E10(s32 arg0, int unused0) {
    char buf[0x80];
    s32 obj;
    s32 r;

    func_0057DA20(buf, "image/%s.png", arg0);
    obj = 0;
    r = func_0048F628(D_00621518, buf, &obj, 0);
    if (r == 0) {
        obj = 0;
        r = func_0048F628(D_00621518, "image/00no_texture.png", &obj, 0);
    }
    return r;
}
/* native correction of src/race/RaceDisplay/func_003A1E10.cpp: returns the texture pointer ($v0) of the last func_0048F628 call (the fallback one if the first returned 0), which the callers store. */
