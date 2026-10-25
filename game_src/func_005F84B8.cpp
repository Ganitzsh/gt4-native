#include "types.h"

struct S003A9738;

extern "C" void Oscillator__setWaveform(struct S003A9738 *arg0, f32 fparg0, f32 fparg1, int unused0, int unused1);

extern "C" void func_005F84B8(char *arg0, f32 fparg0, f32 fparg1, s32 arg1, s32 arg2) {
    Oscillator__setWaveform((struct S003A9738 *)(arg0 + 0x34), fparg0, fparg1, arg1, arg2);
}
/* native correction of src/runtime/unit_005F6EE0/func_005F84B8.cpp: passes the two floats ($f12/$f13, forwarded untouched on the PS2) in Oscillator__setWaveform's parameter order, after the object, and takes them in the same positions */
