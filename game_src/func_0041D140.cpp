/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native correction of the provisional draft of 0041D140 (the driver's arm IK): 00420510 (matched)
 * takes (flag, joint, out, pole, base, target, p0, p1, l1, l2, tol, const float *minAngle). The
 * draft put minAngle before the three floats and added an int: the same call on the EE (the
 * pointer goes to the stack, the floats to $f12-$f14), but natively minAngle read 0. */
typedef unsigned int u128 __attribute__((mode(TI)));

static inline float maxf(float a, float b) { float r; __asm__("max.s %0, %1, %2" : "=f"(r) : "f"(a), "f"(b)); return r; }
static inline float minf(float a, float b) { float r; __asm__("min.s %0, %1, %2" : "=f"(r) : "f"(a), "f"(b)); return r; }
static inline float clampf(float v, float lo, float hi) { return minf(maxf(v, lo), hi); }
static inline float clampr(const float &v, const float &lo, const float &hi) { return minf(maxf(v, lo), hi); }

struct V {
    union { u128 q; struct { float x, y, z, w; }; };
    V() {}
    V(float a, float b, float c) { x = a; y = b; z = c; }
    V(const V &v) { q = v.q; }
    V &operator=(const V &v) { q = v.q; return *this; }
    float &operator[](int i) { return *(float *)((char *)this + i * 4); }
    const float &operator[](int i) const { return *(const float *)((const char *)this + i * 4); }
    V operator+(const V &v) const { V r(*this); r[0] += v[0]; r[1] += v[1]; r[2] += v[2]; return r; }
    V operator-(const V &v) const { V r(*this); r[0] -= v[0]; r[1] -= v[1]; r[2] -= v[2]; return r; }
    V operator-() const { V r; r[0] = -(*this)[0]; r[1] = -(*this)[1]; r[2] = -(*this)[2]; return r; }
    float len2() const { return x * x + y * y + z * z; }
};

struct Q {
    union { u128 q; struct { float x, y, z, w; }; };
    Q() {}
    Q(const Q &o) { q = o.q; }
    Q(const V &v) { q = v.q; }
    Q &operator=(const Q &o) { q = o.q; return *this; }
    float &operator[](int i) { return *(float *)((char *)this + i * 4); }
    const float &operator[](int i) const { return *(const float *)((const char *)this + i * 4); }
    Q conj() const { Q r; r[0] = -(*this)[0]; r[1] = -(*this)[1]; r[2] = -(*this)[2]; r[3] = (*this)[3]; return r; }
};

struct M3 { u128 r[3]; };

struct Angle {
    float x;
    Angle() {}
    Angle(float a) : x(a) {}
    Angle(const Angle &a) : x(a.x) {}
    operator float() const { return x; }
    Angle operator-() const { return Angle(-x); }
    Angle operator+() const { return *this; }
};

struct Seg {
    V pos;
    Q rot;
    void *vt;
    char pad24[0xC];
    V off;
    float len;
    float scale;
    char pad48[8];
};

struct Xform {
    V pos;
    M3 rot;
};

struct Self {
    float f0, f4;
    char pad8[0x10];
    float f18, f1C, f20, f24, f28, f2C;
    char pad30[0x10];
    float f40, f44, f48, f4C;
    char pad50[0x58];
    int fA8, fAC, fB0, fB4;
    char padB8[0x14];
    int fCC;
};

struct Body {
    char pad0[0x2F0];
    V pos;
    Q rot;
    char pad310[0x30];
    V shL, shR;
    Seg upL, upR, foL, foR, s4, shSegL, shSegR, handL, handR;
};

struct Target { V rot; char pad10[0x40]; V handL, handR, dirL, dirR, outL, outR; };
struct Out { char pad0[0xC]; int okL, okR; char pad14[0xC]; };
struct Dbg { char pad0[0x100]; V poleL, poleR; };

extern V D_00622B00;
extern V D_00622B10;
extern V D_00622B30;
extern V D_00622B40;

extern "C" V func_004187A0(Xform *self, const V *v) throw();
extern "C" V func_0041B898(Seg *s) throw();
extern "C" float func_0041B9D8(Seg *s) throw();
extern "C" V func_0041B9E0(Seg *s) throw();
extern "C" void func_0041BAC0(Seg *s, const Angle &a) throw();
extern "C" int func_0041C458(Self *self, Seg *o, Seg *other, const V *target, V *out, const float &factor, float t) throw();
extern "C" int func_0041C6F8(V *out, Seg *seg, const V *dir, const V *from, const V *to, float scale, float ang) throw();
extern "C" int func_00420510(int &flag, Seg &j, Seg &out, const V &pole, const V &base, const V &target,
                             const V &p0, const V &p1, float l1, float l2, float tol, const Angle &minAngle);
extern "C" Angle func_00420FC8(const Angle &deg);
extern "C" M3 *func_00421C58(M3 *out, const Q *q);
extern "C" V func_00421D88(const Q *q);
extern "C" V func_00421DC0(const Q *q);
extern "C" V Numerical_Math__Quaternion__rotate(const Q *q, const V *v);
extern "C" Q *func_004225E0(Q *q, float *angle);
extern "C" Angle func_00423450(const V *a, const V *b);
extern "C" Angle func_00423AA0(const V *a, const V *b, const V *axis);
extern "C" void func_00487830(Q *out, const V *axis, float angle);
extern "C" void func_004878C0(Q *q, const V *axis, float angle);
extern "C" void func_00487EA8(Q *q, float x, float y);
extern "C" void func_004887D8(Q *out, const V *a, const V *b);
extern "C" V func_0048D530(const Q *a, const Q *b);

static inline int reach(Self *self, Seg *o, Seg *other, const V &target, V &out, Angle k, float t) { return func_0041C458(self, o, other, &target, &out, k.x, t); }
static inline void setRot(Q *q, const V &axis, const Angle &a) { func_00487830(q, &axis, a.x); }
static inline V up(const Q &q) { return func_00421DC0(&q); }
static inline Angle twist(const V &a, const V &b, const V &axis) { return func_00423AA0(&a, &b, &axis); }
static inline V rotate(const Q &q, const V &v) { return Numerical_Math__Quaternion__rotate(&q, &v); }
static inline V operator*(Xform &xf, const V &v) { return func_004187A0(&xf, &v); }
static inline float fit(float t, float lo, float hi) { float v = t * (hi - lo); return clampr(v, lo, hi); }
static inline bool greater(float a, float b) { return a > b; }
static inline void euler(Q *q, Angle x, Angle y) { func_00487EA8(q, x, y); }
static inline Angle deg(float d) { return func_00420FC8(Angle(d)); }

extern "C" int func_0041D140(Self *self, Body *body, Target *tg, int unused, Out *out, Dbg *dbg) {
    V e5, e6;
    Q q30, q40;
    Out local;
    int flag;
    if (out == 0) out = &local;
    {
        Xform xf;
        xf.pos = body->pos;
        func_00421C58(&xf.rot, &body->rot);
        body->shSegL.pos = xf * body->shL;
        body->shSegR.pos = xf * body->shR;
        if (self->fA8) {
            V p1 = body->shL + V(func_0041B9D8(&body->shSegL), 0.0f, 0.0f);
            V p2 = body->shR + V(-func_0041B9D8(&body->shSegR), 0.0f, 0.0f);
            p1 = xf * p1;
            p2 = xf * p2;
            p1 = tg->handL - p1;
            p2 = tg->handR - p2;
            float reachL = func_0041B9D8(&body->upL) + func_0041B9D8(&body->foL) + func_0041B9D8(&body->handL);
            float reachR = func_0041B9D8(&body->upR) + func_0041B9D8(&body->foR) + func_0041B9D8(&body->handR);
            float tL = clampf(p1.len2() / (reachL * reachL), 0.0f, 1.0f);
            float tR = clampf(p2.len2() / (reachR * reachR), 0.0f, 1.0f);
            if (tL < 0.5f) tL = 0.0f; else tL = (tL - 0.5f) * 2.0f;
            tR = tR < 0.5f ? 0.0f : (tR - 0.5f) * 2.0f;
            self->f40 = fit(tL + (self->f0 - 0.5f) * 0x1.333332p-2f + (self->f20 - 0.5f) * 0x1.999998p-5f, 0.0f, 0x1.41b2f4p-2f);
            self->f44 = fit(tR + (self->f4 - 0.5f) * 0x1.333332p-2f + (self->f24 - 0.5f) * 0x1.999998p-5f, 0.0f, 0x1.41b2f4p-2f);
        }
        euler(&q30, -Angle(self->f40), +Angle(self->f48));
        euler(&q40, +Angle(self->f44), -Angle(self->f4C));
        body->shSegL.rot = func_0048D530(&body->rot, &q30);
        body->shSegR.rot = func_0048D530(&body->rot, &q40);
    }
    e5 = func_0041B898(&body->shSegL);
    e6 = func_0041B898(&body->shSegR);
    V poleL, poleR;
    if (self->fB0 == 0) {
        Q inv = body->rot.conj();
        V d = rotate(inv, tg->dirL);
        Q r = body->shSegL.rot;
        Angle a(clampf(d.y, 0.0f, 1.0f) * 0x1.41b2f4p-2f);
        func_004225E0(&r, &a.x);
        body->shSegL.rot = r;
    }
    if (self->fB4 == 0) {
        Q inv = body->rot.conj();
        V d = rotate(inv, tg->dirR);
        Q r = body->shSegR.rot;
        Angle a(clampf(d.y, 0.0f, 1.0f) * -0x1.41b2f4p-2f);
        func_004225E0(&r, &a.x);
        body->shSegR.rot = r;
    }
    float halfpi = 0x1.921fb4p+0f;
    e5 = func_0041B898(&body->shSegL);
    e6 = func_0041B898(&body->shSegR);
    {
        V fwd = rotate(tg->rot, D_00622B10);
        float aL = halfpi - func_00423450(&fwd, &tg->dirL);
        float aR = halfpi - func_00423450(&fwd, &tg->dirR);
        func_0041C6F8(&poleL, &body->shSegL, &D_00622B00, &e5, &tg->handL, self->f0, aL);
        func_0041C6F8(&poleR, &body->shSegR, &D_00622B30, &e6, &tg->handR, self->f4, aR);
    }
    if (dbg) {
        dbg->poleL = poleL;
        dbg->poleR = poleR;
    }
    float five = 5.0f;
    float zero = 0.0f;
    {
        func_00420510(flag, body->upL, body->foL, poleL, e5, tg->handL, D_00622B00, D_00622B40, func_0041B9D8(&body->upL), func_0041B9D8(&body->foL) + func_0041B9D8(&body->handL), zero, deg(five));
    }
    {
        func_00420510(flag, body->upR, body->foR, poleR, e6, tg->handR, D_00622B30, D_00622B40, func_0041B9D8(&body->upR), func_0041B9D8(&body->foR) + func_0041B9D8(&body->handR), zero, deg(five));
    }
    body->handL.pos = func_0041B898(&body->foL);
    body->handR.pos = func_0041B898(&body->foR);
    body->handL.rot = body->foL.rot;
    body->handR.rot = body->foR.rot;
    if (self->fCC) {
        Q rL = body->handL.rot;
        Q rR = body->handR.rot;
        V axL, axR;
        axL = func_0041B9E0(&body->handL);
        axR = func_0041B9E0(&body->handR);
        float twL = twist(-up(rL), tg->dirL, axL);
        float twR = twist(-up(rR), tg->dirR, axR);
        func_0041BAC0(&body->handL, Angle(twL * 0x1.999998p-1f));
        func_0041BAC0(&body->handR, Angle(twR * 0x1.999998p-1f));
        func_0041BAC0(&body->foL, Angle(twL * 0x1.999998p-3f));
        func_0041BAC0(&body->foR, Angle(twR * 0x1.999998p-3f));
        return 1;
    }
    float bendL = (self->f20 - 0.5f) * halfpi;
    float bendR = (0.5f - self->f24) * halfpi;
    if (self->f18 > zero) {
        reach(self, &body->handL, &body->foL, tg->dirL, tg->outL, +Angle(-bendL), self->f18);
        func_00420510(flag, body->upL, body->foL, poleL, e5, body->handL.pos, D_00622B00, D_00622B40, func_0041B9D8(&body->upL), func_0041B9D8(&body->foL), 0.02f, deg(five));
        body->handL.pos = func_0041B898(&body->foL);
    }
    if (self->f1C > zero) {
        reach(self, &body->handR, &body->foR, tg->dirR, tg->outR, +Angle(-bendR), self->f1C);
        func_00420510(flag, body->upR, body->foR, poleR, e6, body->handR.pos, D_00622B30, D_00622B40, func_0041B9D8(&body->upR), func_0041B9D8(&body->foR), 0.02f, deg(five));
        body->handR.pos = func_0041B898(&body->foR);
    }
    if (self->fAC != 1) {
        V axF = func_0041B9E0(&body->foL);
        V axH = func_0041B9E0(&body->handL);
        V end = func_0041B898(&body->handL);
        Q d;
        func_004887D8(&d, &axH, &axF);
        body->handL.rot = func_0048D530(&d, &body->handL.rot);
        V ax = func_00421DC0(&body->handL.rot);
        setRot(&d, ax, Angle(bendL));
        body->handL.rot = func_0048D530(&d, &body->handL.rot);
        V cur = func_0041B898(&body->handL);
        end = end - cur + body->handL.pos;
        func_00420510(flag, body->upL, body->foL, poleL, e5, end, D_00622B00, D_00622B40, func_0041B9D8(&body->upL), func_0041B9D8(&body->foL), 0.02f, deg(five));
        body->handL.pos = func_0041B898(&body->foL);
    }
    if (self->fAC != 1) {
        V axF = func_0041B9E0(&body->foR);
        V axH = func_0041B9E0(&body->handR);
        V end = func_0041B898(&body->handR);
        Q d;
        func_004887D8(&d, &axH, &axF);
        body->handR.rot = func_0048D530(&d, &body->handR.rot);
        V ax = func_00421DC0(&body->handR.rot);
        setRot(&d, ax, Angle(bendR));
        body->handR.rot = func_0048D530(&d, &body->handR.rot);
        V cur = func_0041B898(&body->handR);
        end = end - cur + body->handR.pos;
        func_00420510(flag, body->upR, body->foR, poleR, e6, end, D_00622B30, D_00622B40, func_0041B9D8(&body->upR), func_0041B9D8(&body->foR), 0.02f, deg(five));
        body->handR.pos = func_0041B898(&body->foR);
    }
    if (self->f28 != 0.0f) {
        Q r = body->handL.rot;
        V ax = func_0041B9E0(&body->handL);
        Angle a(self->f28);
        func_004878C0(&r, &ax, a.x);
        body->handL.rot = r;
    }
    if (self->f2C != 0.0f) {
        Q r = body->handR.rot;
        V ax = func_0041B9E0(&body->handR);
        Angle a(-self->f2C);
        func_004878C0(&r, &ax, a.x);
        body->handR.rot = r;
    }
    {
        Q rH = body->handL.rot;
        Q rF = body->foL.rot;
        Q rU = body->upL.rot;
        Q rS = body->shSegL.rot;
        V axF = func_0041B9E0(&body->foL);
        V axU = func_0041B9E0(&body->upL);
        V axS = func_0041B9E0(&body->shSegL);
        Angle a1 = func_00423AA0(&func_00421D88(&rF), &func_00421D88(&rH), &axF);
        Angle a2 = func_00423AA0(&func_00421D88(&rU), &func_00421D88(&rF), &axU);
        Angle a3 = func_00423AA0(&func_00421D88(&rS), &func_00421D88(&rU), &axS);
        if (self->fB0 && greater(a1.x, 0x1.921fb4p+0f)) a1.x -= 0x1.921fb4p+2f;
        Angle t(a1.x * 0.7f);
        func_004878C0(&rF, &axF, t.x);
        body->foL.rot = rF;
        t.x = a1.x * 0.25f;
        func_004878C0(&rU, &axU, t.x);
        body->upL.rot = rU;
    }
    {
        Q rH = body->handR.rot;
        Q rF = body->foR.rot;
        Q rU = body->upR.rot;
        Q rS = body->shSegR.rot;
        V axF = func_0041B9E0(&body->foR);
        V axU = func_0041B9E0(&body->upR);
        V axS = func_0041B9E0(&body->shSegR);
        Angle a1 = func_00423AA0(&func_00421D88(&rF), &func_00421D88(&rH), &axF);
        Angle a2 = func_00423AA0(&func_00421D88(&rU), &func_00421D88(&rF), &axU);
        Angle a3 = func_00423AA0(&func_00421D88(&rS), &func_00421D88(&rU), &axS);
        if (self->fB4 && greater(-0x1.921fb4p+0f, a1.x)) a1.x += 0x1.921fb4p+2f;
        Angle t(a1.x * 0x1.54fdf2p-1f);
        func_004878C0(&rF, &axF, t.x);
        body->foR.rot = rF;
        t.x = a1.x * 0x1.54fdf2p-2f;
        func_004878C0(&rU, &axU, t.x);
        body->upR.rot = rU;
    }
    out->okL = flag;
    out->okR = flag;
    return 1;
}
