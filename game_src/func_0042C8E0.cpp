/* Native correction of src/race/unit_004271E8/func_0042C8E0.cpp: a method of the motion target (vtable 00686900 /
 * 00686A50, slot 8), called by 0042B440 as (this, angle). The source leaves `this` out: on the EE it is in $a0 and the
 * floats in $f12.., so it matches; natively `this` is the first stack argument and the callee read it as its first
 * float, every later argument one place off (the skinned people's bones: twisted washers, the GT Auto attendant
 * placed off screen). Here `this` is taken and ignored. */
extern "C" void func_004A79D8(float);
extern "C" void func_0042C8E0(void *self, float angle) {
    func_004A79D8(angle * 57.29578f);
}
