#include "types.h"

extern "C" void *func_00326750(unsigned int size, int align, const char *name);
extern "C" s32 mMotionEvent__structor_0(void *, f32, f32, void *, void *);
extern "C" int func_0026B9E0(void *arg0, void *arg1);

extern "C" void func_002BFA70(void *arg0, void *arg1, void *arg2, f32 fparg0, f32 fparg1) {
    void *s3 = arg0;
    void *s2 = arg1;
    void *s1 = arg2;
    void *s0 = func_00326750(0x28, 4, "RefCounter");
    mMotionEvent__structor_0(s0, fparg0, fparg1, s2, s1);
    void *local = s0;
    func_0026B9E0(s3, &local);
}
/* native correction of src/ui/mMotionEvent/func_002BFA70.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes mMotionEvent__structor_0 */
