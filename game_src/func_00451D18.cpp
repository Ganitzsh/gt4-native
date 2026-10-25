/* compiler: ee-gcc2.96-no-strict-aliasing */
struct Vec3 { float x, y, z; };

struct Obj;
struct VEv { short d; short pad; Vec3 (*f)(void *, float); };
struct VEf { short d; short pad; float (*f)(void *); };
struct VT {
    char p0[0x38];
    VEv e38;
    VEv e40;
    char p48[0x78 - 0x48];
    VEf e78;
    VEf e80;
};
struct Obj { VT *vt; };

struct Self;
extern "C" {
void func_004A53F8();
void func_004A5400();
void func_004A7844(float, float, float);
void func_004A7988(float);
void func_004A79B0(float);
void func_004A79D8(float);
void func_00452418(float, float, Self *);
void func_00452480(Self *);
void func_004524B8(Self *, int);
void func_00452660(Self *, int);
void func_00452998(Self *, int, float);
}

static inline Vec3 vcallv(Obj *o, int off, float t) {
    VEv *e = (VEv *)((char *)o->vt + off);
    return e->f((char *)o + e->d, t);
}
static inline float vcallf(Obj *o, int off) {
    VEf *e = (VEf *)((char *)o->vt + off);
    return e->f((char *)o + e->d);
}

extern "C" void func_00451D18(Self *self, Obj *obj, int a, int b, int c, float t) {
    Vec3 v;
    if (c != 0) {
        func_004A53F8();
        v = vcallv(obj, 0x40, t);
        float *py = &v.y;
        float *pz = &v.z;
        func_004A7844(v.x, *py, *pz);
        v = vcallv(obj, 0x38, t);
        func_004A79B0(*py);
        func_004A7988(v.x);
        func_004A79D8(*pz);
    }
    func_00452418(0.008f, vcallf(obj, 0x80), self);
    if (a != 0) {
        func_004524B8(self, 0);
    }
    if (b != 0) {
        func_00452660(self, 0);
    }
    float r = vcallf(obj, 0x78);
    if (r > 0.0f) {
        func_00452998(self, 0, r);
    }
    func_00452480(self);
    if (c != 0) {
        func_004A5400();
    }
}
/* native correction of src/engine/unit_00450248/func_00451D18.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_00452418 */
