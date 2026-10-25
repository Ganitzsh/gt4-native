/* compiler: ee-gcc2.96-nosib */
typedef long long s64;

extern "C" int func_00444690(void *self, s64 v, int unused0);
extern "C" int func_00444480(void *self, s64 v);

extern "C" int func_00444440(void *self, s64 v, int unused0) {
    if ((v >> 32) == 0x26) {
        return func_00444690(self, v, unused0);
    } else {
        return func_00444480(self, v);
    }
}
/* native correction of src/race/unit_004271E8/func_00444440.cpp: returns the result of func_00444690 / func_00444480 (the call on each path, left in $v0 on the PS2) */
