struct B80 { char x[80]; };
struct VE { short d, p; int (*fn)(void *, void *); };
extern "C" int func_0043A398(void *a, int n);

extern "C" int func_004398E0(char *self, char *dst) {
    *(B80 *)dst = *(B80 *)(self + 4);
    func_0043A398(dst, 0x50);
    VE *v = (VE *)(*(char **)self + 0x20);
    return v->fn(self + v->d, dst);
}
/* native correction of src/race/unit_004271E8/func_004398E0.cpp: returns the value of the closing virtual call (slot +0x20), which the PS2 left in $v0 */
