/* Native correction of src/race/unit_004271E8/func_0042C870.cpp: a method of the motion target (vtable 00686900 /
 * 00686A50, slot 5), called by 0042B440 as (this, x, y, z). The source leaves `this` out: on the EE it is in $a0 and the
 * floats in $f12.., so it matches; natively `this` is the first stack argument and the callee read it as its first
 * float, every later argument one place off (the skinned people's bones: twisted washers, the GT Auto attendant
 * placed off screen). Here `this` is taken and ignored. */
extern "C" void func_004A7844(float, float, float);
extern "C" void func_0042C870(void *self, float x, float y, float z) {
    func_004A7844(x, y, z);
}
