/* compiler: ee-gcc2.96-no-strict-aliasing */
typedef unsigned int u128 __attribute__((mode(TI)));
struct V {
    float x, y, z, w;
    V() {}
    V(const V &v) { *(u128 *)this = *(const u128 *)&v; }
    V &operator=(const V &v) { *(u128 *)this = *(const u128 *)&v; return *this; }
    float &operator[](int i) { return *(float *)((char *)this + i * 4); }
    const float &operator[](int i) const { return *(const float *)((const char *)this + i * 4); }
    V operator*(float t) const { V r(*this); r[0] *= t; r.y *= t; r.z *= t; return r; }
    V operator+(const V &v) const { V r(*this); r[0] += v[0]; r[1] += v[1]; r[2] += v[2]; return r; }
    V &operator+=(const V &v) { x = (*this)[0] + v[0]; y = (*this)[1] + v[1]; z = (*this)[2] + v[2]; return *this; }
} __attribute__((aligned(16)));
struct Angle { float x; Angle(float a) : x(a) {} Angle(const Angle &a) : x(a.x) {} Angle &operator=(float a) { x = a; return *this; } };
struct S;
struct Obj { int mode; char pad[0xC]; V a; V b; V c; float d, e, f; char pad4c[4]; V off; char pad60[0x10]; float scale; };
struct SinCos { float s, c; SinCos(const Angle &a); } __attribute__((aligned(8)));
extern "C" void func_0057D8A0(float *, float *, float);
inline SinCos::SinCos(const Angle &a) { func_0057D8A0(&s, &c, a.x); }
extern "C" V func_00414E78(S *) throw();
extern "C" void func_00415E90(Obj *, V *, Angle, Angle, S *, float) throw();
extern "C" void func_00416040(Obj *, V *, V *, Angle, S *) throw();
extern "C" int func_00415AF8(V *, S *, float, float) throw();
extern "C" V func_00415148(S *, const Angle &, float, float) throw();
extern "C" V func_004150A0(S *, int, float, float) throw();
extern "C" void func_00423BF8(V *, const V *, const V *, const V *, float); /* (out, a, b, fallback, t) as its draft defines it */
extern V D_00622B10;
static inline int limit(int mode, const Angle &v) {
    if (mode == 0) {
        if (v.x < -0x1.921fb4p+0f) return 1;
    } else {
        if (v.x > 0x1.921fb4p+0f) return 1;
    }
    return 0;
}
extern "C" int func_00416390(Obj *o, float *pa, float *pb, S *path, float t, float phase) {
    V q = func_00414E78(path) + o->off;
    if (phase >= 0.0f && phase <= 0.25f) {
        float u = (phase - 0.0f) / (0.25f - 0.0f);
        V p;
        func_00415E90(o, &p, Angle(*pa), Angle(*pb), path, t);
        V v = p * (1.0f - u) + q * u;
        o->a = v;
        if (func_00415AF8(&v, path, *pb, 1.0f - u)) o->a += v;
        o->d = 0.5f;
    } else if (phase >= 0.25f && phase <= 0.4f) {
        o->a = q;
        o->d = 0.5f;
    } else if (phase >= 0.25f && phase <= 0.6f) {
        o->a = q;
        o->d = 0.5f;
    } else if (phase >= 0.6f && phase <= 0.75f) {
        o->a = q;
        o->d = 0.5f;
    } else if (phase >= 0.75f && phase <= 1.0f) {
        float u = (phase - 0.75f) / (1.0f - 0.75f);
        SinCos sa((Angle(*pa)));
        SinCos sb((Angle(*pb)));
        V p;
        func_00415E90(o, &p, Angle(*pa), Angle(*pb), path, t);
        V v = q * (1.0f - u) + p * u;
        o->a = v;
        if (func_00415AF8(&v, path, *pb, u)) o->a += v;
        o->d = 0.5f;
    }
    Angle blend(0.0f);
    if (phase >= 0.0f && phase <= 0.2f) {
        blend = (*pa * (1.0f - t) + *pb * t) * (1.0f - (phase - 0.0f) / (0.2f - 0.0f));
        func_00416040(o, &o->b, &o->c, blend, path);
        o->e = 1.0f;
        o->f = 0.0f;
    } else if (phase >= 0.2f && phase <= 0.8f) {
        float w = (phase - 0.2f) / (0.8f - 0.2f);
        if (w <= 0.2f) {
            float k = w * 5.0f;
            V v = func_00415148(path, Angle(0.0f), 0.0f, 1.0f);
            func_00423BF8(&o->c, &v, &D_00622B10, 0, k);
            o->e = 1.0f - k;
            o->f = k * o->scale;
        } else if (w <= 0.8f) {
            o->c = D_00622B10;
            o->f = o->scale;
            o->e = 0.0f;
        } else {
            float k = (w - 0.8f) * 5.0f;
            V v = func_00415148(path, Angle(0.0f), 0.0f, 1.0f);
            float r = 1.0f - k;
            func_00423BF8(&o->c, &D_00622B10, &v, 0, k);
            o->e = k;
            o->f = r * o->scale;
        }
        o->b = func_004150A0(path, o->mode, 0.0f, 1.0f);
    } else if (phase >= 0.8f && phase <= 1.0f) {
        blend = (*pa * (1.0f - t) + *pb * t) * ((phase - 0.8f) / (1.0f - 0.8f));
        func_00416040(o, &o->b, &o->c, blend, path);
        o->e = 1.0f;
        o->f = 0.0f;
    }
    return limit(o->mode, Angle(blend));
}
/* native correction of src/race/unit_00415E90/func_00416390.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_004150A0; argument order (argcheck.py orders): func_0057D8A0 takes (sine, cosine, x) */
