/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native copy of 00452008 (matched): the wheels' positions and directions from the car object's getters (+0x50,
 * +0x48: 003D72A0, 003D71D0, index then time). The matched source passes the time first, the same call on the EE
 * (the float in its own register) but not natively: the index got the time's bits and the time the index's. */
struct Vec3 { float x, y, z; };

struct VEv { short d; short pad; Vec3 (*f)(void *, int, float); };
struct VT {
    char p0[0x48];
    VEv e48;
    VEv e50;
};
struct Obj { VT *vt; };
struct Self;

extern "C" {
void func_004514A0(Self *, int, float, float, float) throw();
void func_00451518(Self *, unsigned, float, float, float, float) throw();
void func_00452808(Self *, void *);
void func_004595C8(Obj *, float);
void func_00459610(Obj *, float);
void func_004A53F8();
void func_004A5400();
}

static inline Vec3 vcallv(Obj *o, int off, float t, int i) {
    VEv *e = (VEv *)((char *)o->vt + off);
    return e->f((char *)o + e->d, i, t);
}

extern "C" void func_00452008(Self *self, Obj *obj, int flag, void *other, float t) {
    if (flag != 0) {
        func_004A53F8();
        func_004595C8(obj, t);
        func_00459610(obj, t);
    }
    Vec3 v;
    int i = 0;
    do {
        v = vcallv(obj, 0x50, t, i);
        float *py = &v.y;
        float *pz = &v.z;
        func_004514A0(self, i, v.x, *py, *pz);
        v = vcallv(obj, 0x48, t, i);
        func_00451518(self, i, v.x, *py, *pz, 0.0f);
        i++;
    } while (i < 4);
    func_00452808(self, other);
    if (flag != 0) {
        func_004A5400();
    }
}
