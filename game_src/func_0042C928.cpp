/* Native correction of src/race/unit_004271E8/func_0042C928.cpp: a method of the motion target (vtable 00686900 /
 * 00686A50, slot 19), called by 0042B440 as (this, x, y, z, w (a quaternion)). The source leaves `this` out: on the EE it is in $a0 and the
 * floats in $f12.., so it matches; natively `this` is the first stack argument and the callee read it as its first
 * float, every later argument one place off (the skinned people's bones: twisted washers, the GT Auto attendant
 * placed off screen). Here `this` is taken and ignored. */
struct V4 { float x, y, z, w; };
struct M4 { float m[16]; };
extern "C" void func_0048A970(V4 *, M4 *, float, float, float);
extern "C" void func_004A7698(M4 *);
extern "C" void func_0042C928(void *self, float x, float y, float z, float w) {
    M4 m;
    V4 v;
    V4 *p = &v;
    p->x = x;
    p->y = y;
    p->z = z;
    p->w = w;
    func_0048A970(&v, &m, 0.0f, 0.0f, 0.0f);
    func_004A7698(&m);
}
