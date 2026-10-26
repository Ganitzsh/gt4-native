/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native correction of the provisional draft of 0041E678 (the driver's leg IK): 00420510 (matched)
 * takes (flag, joint, out, pole, base, target, p0, p1, l1, l2, tol, const float *minAngle). The
 * draft put minAngle before the three floats and added an int: the same call on the EE (the
 * pointer goes to the stack, the floats to $f12-$f14), but natively minAngle read 0. */
typedef unsigned int u128 __attribute__((mode(TI)));

static inline float maxf(float a, float b) { float r; __asm__("max.s %0, %1, %2" : "=f"(r) : "f"(a), "f"(b)); return r; }
static inline float minf(float a, float b) { float r; __asm__("min.s %0, %1, %2" : "=f"(r) : "f"(a), "f"(b)); return r; }
static inline float rsqrtf_(float a, float b) { float r; __asm__("rsqrt.s %0, %1, %2" : "=f"(r) : "f"(a), "f"(b)); return r; }
static inline float clampf(const float &v, float lo, float hi) { return minf(maxf(v, lo), hi); }

struct V {
    union { u128 q; struct { float x, y, z, w; }; };
    V() {}
    V(float a, float b, float c) { x = a; y = b; z = c; }
    V(const V &v) { q = v.q; }
    V &operator=(const V &v) { q = v.q; return *this; }
    float &at(int i) { return (&x)[i]; }
    const float &at(int i) const { return (&x)[i]; }
    float &operator[](int i) { return at(i); }
    const float &operator[](int i) const { return at(i); }
    V &operator+=(const V &v) { (*this)[0] = (*this)[0] + v[0]; (*this)[1] = (*this)[1] + v[1]; (*this)[2] = (*this)[2] + v[2]; return *this; }
    V &operator-=(const V &v) { (*this)[0] = (*this)[0] - v[0]; (*this)[1] = (*this)[1] - v[1]; (*this)[2] = (*this)[2] - v[2]; return *this; }
    V operator+(const V &v) const { V r(*this); r += v; return r; }
    V operator-(const V &v) const { V r(*this); r -= v; return r; }
    V operator-() const { V r; r[0] = -(*this)[0]; r[1] = -(*this)[1]; r[2] = -(*this)[2]; return r; }
    float dot(const V &v) const { return x * v.x + y * v.y + z * v.z; }
    V normalized() const { V r(*this); float k = rsqrtf_(1.0f, r.dot(r)); r[0] *= k; r.y *= k; r.z *= k; return r; }
};

struct H {
    union { u128 q; struct { float x, y, z, w; }; };
    H() {}
    H(const V &v) { set(v[0], v[1], v[2], 1.0f); }
    void set(float a, float b, float c, float d) { x = a; y = b; z = c; w = d; }
    H(const H &v) { q = v.q; }
    H &operator=(const H &v) { q = v.q; return *this; }
};

struct Q {
    union { u128 q; struct { float x, y, z, w; }; };
    Q() {}
    Q(const Q &o) { q = o.q; }
    Q &operator=(const Q &o) { q = o.q; return *this; }
};

struct M3 {
    u128 r[3];
    M3() {}
    M3(const M3 &m) { r[0] = m.r[0]; r[1] = m.r[1]; r[2] = m.r[2]; }
    M3 &operator=(const M3 &m) { r[0] = m.r[0]; r[1] = m.r[1]; r[2] = m.r[2]; return *this; }
};
struct M4 {
    u128 r[4];
    M4() {}
    M4(const M4 &m) { r[0] = m.r[0]; r[1] = m.r[1]; r[2] = m.r[2]; r[3] = m.r[3]; }
};

struct Angle {
    float x;
    Angle() {}
    Angle(float a) : x(a) {}
    Angle(const Angle &a) : x(a.x) {}
    operator float() const { return x; }
    Angle operator-() const { return Angle(-x); }
};

struct Seg { V pos; Q rot; char opaque[0x30]; };
struct Body { V pos; Q rot; char pad20[0x30]; V left, right; Seg seg[9]; };
struct Frame;
struct Params { float v[16]; };
struct Target { char pad[0x20]; V left, right; };
struct Res { int v[8]; };
struct Debug { char pad[0xD0]; V left, right; };

extern V D_00622B20;
extern V D_00622B40;

extern "C" M4 func_00418880(Body *b) throw();
extern "C" V func_004187A0(Frame *f, const V *v) throw();
extern "C" V func_0041B898(Seg *s) throw();
extern "C" V func_0041B9E0(Seg *s) throw();
extern "C" float func_0041B9D8(Seg *s) throw();
extern "C" M3 func_00421C58(const Q *q);
extern "C" V func_00421D50(const Q *q);
extern "C" V func_00421DC0(const Q *q);
extern "C" void func_00486B38(M3 *dst, const M3 *src);
extern "C" void func_00489300(V *out, const M3 *m, const V *v);
extern "C" void func_004894E8(H *out, const M4 *m, const H *v);
extern "C" void func_00487A58(Q *out, const Q *q, float a);
extern "C" void func_00488948(Q *q, const V *a, const V *b);
extern "C" void func_00487830(Q *q, const V *axis, float a);
extern "C" void func_004878C0(Q *q, const V *axis, float a);
extern "C" void func_0048D510(Q *q, const Q *r);
extern "C" Angle func_00420FC8(const Angle &deg);
extern "C" int func_00420510(int &flag, Seg &j, Seg &out, const V &pole, const V &base, const V &target,
                             const V &p0, const V &p1, float l1, float l2, float tol, const Angle &minAngle);
extern "C" float func_0059D2A0(float);

struct Frame {
    V pos;
    M3 rot;
    Frame(const Body &b) : pos(b.pos), rot(func_00421C58(&b.rot)) {}
};

static inline H mul(const M4 &m, const H &v) { H r; func_004894E8(&r, &m, &v); return r; }
static inline V mul(const M3 &m, const V &v) { V r; func_00489300(&r, &m, &v); return r; }
static inline M3 transpose(const M3 &m) { M3 r; func_00486B38(&r, &m); return r; }
static inline void turn(Q &q, const Angle &a) { func_00487A58(&q, &q, a); }
static inline void setRot(Q &q, const V &v, const Angle &a) { func_00487830(&q, &v, a); }
static inline void addRot(Q &q, const V &v, const Angle &a) { func_004878C0(&q, &v, a); }
static inline Angle deg(float d) { return func_00420FC8(Angle(d)); }
static inline float acosf_(float c) { return 0x1.921fb4p+0f - func_0059D2A0(clampf(c, -1.0f, 1.0f)); }

extern "C" int func_0041E678(Params *p, Body *b, Target *t, void *unused, Res *res, Debug *dbg) {
    H left(b->left), right(b->right);
    Res dummy;
    if (!res) res = &dummy;
    int flag;
    left = mul(func_00418880(b), left);
    right = mul(func_00418880(b), right);
    V normal, negative, pole1, pole2;
    {
        M3 m = func_00421C58(&b->rot);
        M3 inv = transpose(m);
        Frame f(*b);
        normal.x = ((float *)&m)[0];
        normal.y = ((float *)&m)[1];
        normal.z = ((float *)&m)[2];
        negative = -normal;
        V d1 = t->left - func_004187A0(&f, &b->left);
        V d2 = t->right - func_004187A0(&f, &b->right);
        d1 = mul(inv, d1).normalized();
        d2 = mul(inv, d2).normalized();
        Q q1, q2;
        turn(q1, Angle(p->v[2] * 0.5f * 0x1.921fb4p+1f));
        turn(q2, Angle(p->v[3] * -0.5f * 0x1.921fb4p+1f));
        func_00488948(&q1, &D_00622B40, &d1);
        func_00488948(&q2, &D_00622B40, &d2);
        pole1 = func_00421DC0(&q1);
        pole2 = func_00421DC0(&q2);
        pole1 = mul(m, pole1);
        pole2 = mul(m, pole2);
    }
    if (dbg) {
        dbg->right = pole2;
        dbg->left = pole1;
    }
    {
        Angle a = deg(5.0f);
        float l1 = func_0041B9D8(&b->seg[0]), l2 = func_0041B9D8(&b->seg[2]);
        V base(left.x, left.y, left.z);
        func_00420510(flag, b->seg[0], b->seg[2], pole1, base, t->left, D_00622B40, D_00622B20, l1, l2, 0.0f, a);
        res->v[1] = flag;
    }
    {
        Angle a = deg(5.0f);
        float l1 = func_0041B9D8(&b->seg[1]), l2 = func_0041B9D8(&b->seg[3]);
        V base(right.x, right.y, right.z);
        func_00420510(flag, b->seg[1], b->seg[3], pole2, base, t->right, D_00622B40, D_00622B20, l1, l2, 0.0f, a);
        res->v[2] = flag;
    }
    {
        V v1 = func_00421DC0(&b->seg[2].rot), v2 = func_00421DC0(&b->seg[3].rot);
        float a = 0x1.921fb4p+0f - acosf_(v1.dot(normal));
        float c = 0x1.921fb4p+0f - acosf_(v2.dot(negative));
        if (a > 0x1.41b2f4p+0f) a = 0x1.41b2f4p+0f; else if (a < 0.0f) a = 0.0f;
        if (c > 0x1.41b2f4p+0f) c = 0x1.41b2f4p+0f; else if (c < 0.0f) c = 0.0f;
        a *= p->v[4] * 0.5f;
        c *= p->v[5] * 0.5f;
        Q q1 = b->seg[2].rot, q2 = b->seg[3].rot, d1, d2;
        setRot(d1, func_0041B9E0(&b->seg[2]), Angle(a));
        setRot(d2, func_0041B9E0(&b->seg[3]), Angle(-c));
        func_0048D510(&q1, &d1);
        func_0048D510(&q2, &d2);
        b->seg[2].rot = q1;
        b->seg[3].rot = q2;
        b->seg[4].pos = func_0041B898(&b->seg[2]);
        b->seg[5].pos = func_0041B898(&b->seg[3]);
        func_0048D510(&q1, &d1);
        func_0048D510(&q2, &d2);
        b->seg[4].rot = q1;
        b->seg[5].rot = q2;
        float t1 = (p->v[12] - 0.5f) * 0x1.41b2f4p+0f, t2 = (p->v[13] - 0.5f) * -0x1.41b2f4p+0f;
        addRot(q1, func_0041B9E0(&b->seg[4]), Angle(t1));
        addRot(q2, func_0041B9E0(&b->seg[5]), Angle(t2));
        float u = p->v[14], v = -Angle(p->v[15]);
        V axis1 = func_00421D50(&q1), axis2 = -func_00421D50(&q2);
        addRot(q1, axis1, Angle(u));
        addRot(q2, axis2, Angle(v));
        b->seg[4].rot = q1;
        b->seg[5].rot = q2;
    }
    return 1;
}
