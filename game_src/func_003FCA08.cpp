/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 003FCA08: the lanes of the 128-bit point its callee returns read as floats (the original moves them
   out of a 128-bit register: MTC1, PCPYUD, DSRA32). */
typedef int I128 __attribute__((mode(TI)));
struct Obj;
struct S;
struct Vec3 { float x, y, z; };
extern "C" I128 func_003952C0(Obj *self, int idx);
extern "C" float func_00395228(S *arg0, int arg1);
extern "C" float func_0036EAD8(float a);
static inline float quad_lane(const I128 &v, int k) { return ((const float *)&v)[k]; }
extern "C" void func_003FCA08(Vec3 *out, float *angle, Obj *obj, int idx) {
    I128 q = func_003952C0(obj, idx);
    out->x = quad_lane(q, 0);
    out->z = quad_lane(q, 2);
    out->y = quad_lane(q, 1);
    float deg = func_00395228((S *)obj, idx) * 57.29578f;
    *angle = deg;
    *angle = func_0036EAD8(360.0f - deg);
}
