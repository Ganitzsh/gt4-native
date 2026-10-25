struct B80 { char x[80]; };
struct VE { short d, p; int (*fn)(void *, void *); };
extern "C" int func_0043A3C8(void *a, int n);

extern "C" int func_00439A08(char *self, char *src) {
    *(B80 *)(self + 4) = *(B80 *)src;
    func_0043A3C8(src, 0x50);
    VE *v = (VE *)(*(char **)self + 0x20);
    return v->fn(self + v->d, src);
}
/* native correction of src/race/unit_004271E8/func_00439A08.cpp: returns the value of the closing virtual call (slot +0x20), which the PS2 left in $v0 */
