/* compiler: ee-gcc2.96-no-strict-aliasing */
/* native stand-in, not matched (1222 instructions, never drafted): the rider's handle pose of the
   race unit 00415E90, called by func_00416FD8 (out, body, tg, frame). Builds the eight handle solvers
   (single-handed, fixed, easy, free, left and right), picks a pair by self->BC / tg->12C / self->2D4
   (single-handed: blends the three key frames self->2F0/300/310 by self->350 into their hold
   point), lets each solve (virtual slot 1), then computes the shoulder point (rotated by the lean
   angle), the elbow bend angles (self->40/44), the arm twist from the solvers' hand points (law of
   cosines, func_004233C0), and writes the transformed points into the frame `out`. Written from
   the assembly block by block. */
typedef int s32;
typedef float f32;

struct V { f32 x, y, z, w; } __attribute__((aligned(16)));
struct Q { f32 x, y, z, w; } __attribute__((aligned(16)));
struct M3 { V r[3]; };
struct Transform { V pos; M3 m; } __attribute__((aligned(16)));

/* a handle solver (HandleSolverBase and its four kinds); the single-handed one is 0x78 bytes */
struct Solver {
    s32 flag;
    char pad4[0xC];
    V v10;      /* hand point */
    V v20;      /* twist axis */
    V v30;      /* arm direction */
    f32 f40, f44, f48;
    char *vt;   /* 0x4C */
    /* SingleHandedHandleSolver only */
    V v50;
    f32 f60, f64, f68, f6C, f70;
    s32 f74;
    char pad78[8];
} __attribute__((aligned(16)));

struct SolveEnt { short delta, index; s32 (*fn)(void *, f32 *, f32 *, void *, f32, f32); };
struct ValueEnt { short delta, index; f32 (*fn)(void *); };

extern "C" void func_00415CD8(Solver *self, s32 side);              /* SingleHandedHandleSolver */
extern "C" void func_004154E0(Solver *self, s32 side);              /* FixedHandleSolver */
extern "C" void func_00415470(Solver *self, s32 side);              /* EasyHandleSolver */
extern "C" void func_00415608(Solver *self, s32 side, void *ref);   /* FreeHandleSolver */
extern "C" f32 func_00416EB0(void *self);
extern "C" f32 func_00417110(void *self, void *body, f32 f);
extern "C" void func_00418818(Transform *t, V *out, const V *in);  /* out = t->m * in + t->pos */
extern "C" char *func_0041A988(char *body, unsigned int part);
extern "C" f32 func_0041B9D8(char *seg);                            /* seg->f40 */
extern "C" M3 *func_00421C58(M3 *out, void *src);
extern "C" void func_00421E50(V *out, const void *q, const V *v);  /* Numerical_Math::Quaternion::rotate (V returned in memory) */
extern "C" f32 *func_004233C0(f32 *out, f32 a, f32 b, f32 c);
extern "C" void func_00487830(f32 *out, const f32 *axis, f32 angle);
extern "C" void func_00487950(Q *q, f32 angle);
extern "C" f32 func_0057D1F0(f32 x);
/* the native build's draft of func_0057D8A0 (sincos) takes (sine, cosine, x) */
extern "C" void func_0057D8A0(f32 *sine, f32 *cosine, f32 x);

static inline f32 fmax_(f32 a, f32 b) { f32 r; __asm__("max.s %0, %1, %2" : "=f"(r) : "f"(a), "f"(b)); return r; }
static inline f32 fmin_(f32 a, f32 b) { f32 r; __asm__("min.s %0, %1, %2" : "=f"(r) : "f"(a), "f"(b)); return r; }
static inline f32 fsqrt_(f32 a) { f32 r; __asm__("sqrt.s %0, %1" : "=f"(r) : "f"(a)); return r; }
static inline f32 clamp_(f32 v, f32 lo, f32 hi) { return fmin_(fmax_(v, lo), hi); }

#define F(p, o) (*(f32 *)((char *)(p) + (o)))
#define I(p, o) (*(s32 *)((char *)(p) + (o)))
#define VV(p, o) (*(V *)((char *)(p) + (o)))

static inline s32 solve(Solver *s, f32 *a, f32 *b, void *tg, f32 x, f32 y) {
    SolveEnt *e = (SolveEnt *)(s->vt + 0x10);
    return e->fn((char *)s + e->delta, a, b, tg, x, y);
}

/* the value of a body segment (virtual slot at +0x28 of the vtable at +0x20) */
static inline f32 seg_value(char *seg) {
    ValueEnt *e = (ValueEnt *)(*(char **)(seg + 0x20) + 0x28);
    return e->fn(seg + e->delta);
}

static inline s32 pick(char *tg, Solver *s) {
    s32 m = I(tg, 0x120);
    if ((m == 1 && !s->flag) || (m == 2 && s->flag)) return 1;
    return 0;
}

static inline f32 mag(const V &v) { return fsqrt_(v.x * v.x + v.y * v.y + v.z * v.z); }
static inline V sub(const V &a, const V &b) { V r = a; r.x = a.x - b.x; r.y = a.y - b.y; r.z = a.z - b.z; return r; }

/* (x - lo) / (0 - lo) clamped to [0, 1], computed as the original: k * x + k * (0 - lo) */
static inline f32 unit(f32 x, f32 lo) {
    f32 d = 0.0f - lo;
    f32 k = 1.0f / d;
    return clamp_(k * x + k * d, 0.0f, 1.0f);
}

extern "C" s32 func_004173B0(char *self, char *body, char *tg, char *out) {
    Solver sh0, sh1, fx0, fx1, ez0, ez1, fr0, fr1;
    Solver *L, *R;
    V H, C1, C2, L30, R30, L10, R10, rot, Vb, tmp;
    Q q3B0, q3C0, q3D0;
    V axis;
    Transform T;
    f32 a220, a228;
    s32 r0, r1;
    f32 lean, len, ang, sn, cs;

    VV(out, 0) = VV(tg, 0x10);
    func_00415CD8(&sh0, 0);
    func_00415CD8(&sh1, 1);
    func_004154E0(&fx0, 0);
    func_004154E0(&fx1, 1);
    func_00415470(&ez0, 0);
    func_00415470(&ez1, 1);
    func_00415608(&fr0, 0, tg + 0x40);
    func_00415608(&fr1, 1, tg + 0x40);

    if (I(self, 0xBC) == 0) {
        L = &ez0; R = &ez1;
        I(self, 0xAC) = 0;
    } else if (I(tg, 0x12C) == 1) {
        I(self, 0xAC) = 1;
        L = &fx0; R = &fx1;
    } else if (I(self, 0x2D4) != 0) {
        L = &fr0; R = &fr1;
    } else {
        /* single-handed: blend the key frames 2F0/300/310 (and their parameters) by self->350 */
        V P;
        f32 f4 = 0.0f, f5 = 0.0f, f6 = 0.0f, f7 = 0.0f;
        f32 t = F(self, 0x350);
        P.x = 0.0f; P.y = 0.0f; P.z = 0.0f; P.w = 0.0f;
        int k0 = -1, k1 = 0;
        f32 u = 0.0f, w = 0.0f;   /* weights of key k0 and key k1 */
        if (t >= 0.0f && t <= 1.0f) {
            k0 = 0; k1 = 1; w = t; u = 1.0f - t;
        } else if (t <= 2.0f) {
            k0 = 1; k1 = 2; w = t - 1.0f; u = 1.0f - w;
        } else if (t <= 3.0f) {
            k0 = 2; k1 = 0; w = t - 2.0f; u = 1.0f - w;
        }
        if (k0 >= 0) {
            V A = VV(self, 0x2F0 + k0 * 0x10);
            V B = VV(self, 0x2F0 + k1 * 0x10);
            A.x *= u; A.y *= u; A.z *= u;
            B.x *= w; B.y *= w; B.z *= w;
            P = A;
            P.x = A.x + B.x; P.y = A.y + B.y; P.z = A.z + B.z;
            f7 = F(self, 0x250 + k0 * 8) * u + F(self, 0x250 + k1 * 8) * w - F(self, 0x248);
            f6 = F(self, 0x254 + k0 * 8) * u + F(self, 0x254 + k1 * 8) * w - F(self, 0x24C);
            f4 = F(self, 0x320 + k0 * 8) * u + F(self, 0x320 + k1 * 8) * w;
            f5 = F(self, 0x324 + k0 * 8) * u + F(self, 0x324 + k1 * 8) * w;
        }
        if (pick(tg, &sh0)) {
            sh0.v50 = P;
            sh0.f68 = F(self, 0x238);
            sh0.f6C = F(self, 0x240);
            sh0.f60 = f4;
            sh0.f64 = f5;
        }
        if (pick(tg, &sh1)) {
            sh1.v50 = P;
            sh1.f60 = f4;
            sh1.f64 = f5;
            sh1.f68 = F(self, 0x23C);
            sh1.f6C = F(self, 0x244);
        }
        sh0.f70 = f7;
        sh1.f70 = f6;
        sh1.f74 = I(self, 0x2E4);
        sh0.f74 = I(self, 0x2E4);
        L = &sh0; R = &sh1;
        I(self, 0xAC) = 0;
    }

    /* each side solves its hand */
    a220 = F(self, 0x220);
    a228 = F(self, 0x228);
    r0 = solve(L, &a220, &a228, tg, F(self, 0x230), func_00416EB0(self));
    a220 = F(self, 0x224);
    a228 = F(self, 0x22C);
    r1 = solve(R, &a220, &a228, tg, F(self, 0x234), func_00416EB0(self));
    if (I(self, 0x2DC) == 1)
        r0 = 1;
    else if (I(self, 0x2DC) == 2)
        r0 = 0;
    if (I(self, 0x2E0) == 1)
        r1 = 1;
    else if (I(self, 0x2E0) == 2)
        r1 = 0;
    I(self, 0xB0) = r0;
    I(self, 0xB4) = r1;

    if (I(self, 0x200) != 0) {
        lean = F(self, 0x5C);
    } else {
        f32 hi = 0x1.921fb4p-1f;
        f32 lo = -hi;
        lean = ((F(self, 0x5C) - lo) / (hi - lo) - 0.5f) * 0x1.333332p-3f * 0x1.921fb4p+1f;
    }

    len = func_0041B9D8(body);
    {
        f32 b = func_0041B9D8(body + 0x250);
        f32 c = func_0041B9D8(body + 0x2A0);
        f32 d = func_0041B9D8(body + 0x2F0);
        len = len + b + c + d + F(self, 0x2D0);
    }
    H = VV(tg, 0x40);
    C1 = VV(tg, 0x20);
    C1.x += F(self, 0x280); C1.y += F(self, 0x284); C1.z += F(self, 0x288);
    C2 = VV(tg, 0x30);
    C2.x += F(self, 0x290); C2.y += F(self, 0x294); C2.z += F(self, 0x298);
    L30 = L->v30;
    R30 = R->v30;
    L10 = L->v10;
    R10 = R->v10;
    len *= 1.0f - func_0057D1F0(F(self, 0x268)) * 0x1.999998p-5f;
    if (I(self, 0x2D8) == 0)
        ang = func_00417110(self, body, len - seg_value(body));
    else
        ang = F(self, 0x268);
    func_0057D8A0(&sn, &cs, ang);

    /* the shoulder point: (0, len cos, len sin) rotated by `lean` about (0, -sin, cos) */
    Vb.x = 0.0f;
    Vb.y = len * cs;
    Vb.z = len * sn;
    Vb.w = 0.0f;
    axis.x = 0.0f;
    axis.y = -sn;
    axis.z = cs;
    axis.w = 0.0f;
    func_00487830(&q3B0.x, &axis.x, lean);
    func_00421E50(&rot, &q3B0, &Vb);
    Vb = rot;
    Vb.x = rot.x + H.x + F(self, 0x2B0);
    Vb.y = rot.y + H.y + F(self, 0x2B4);
    Vb.z = rot.z + H.z + F(self, 0x2B8);

    if (I(self, 0xBC) != 0) {
        /* elbow bend of each arm from the hand points, and the shoulder roll */
        f32 x, z1, s, r, p22, p23, p25;
        p22 = 0x1.41b2f4p-2f;
        p23 = p22;
        /* left arm, x: (L10.x - Vb.x) mapped from [value of part 15, 0] */
        x = L10.x - Vb.x;
        r = unit(x, seg_value(func_0041A988(body, 0xF))) * 0x1.e28c6ep-3f;
        z1 = L10.z - Vb.z;
        p25 = r * 0x1.999998p-5f;
        p22 = r + p22;
        p23 = p23 - r * 0.25f;
        /* left arm, z: mapped from [-(sum of three segment lengths), 0] */
        s = func_0041B9D8(body + 0x360);
        s = s + func_0041B9D8(body + 0x400);
        s = s + func_0041B9D8(body + 0x590);
        p25 = p25 + 0.0f;
        r = unit(z1, s) * -0x1.e28c6ep-2f;
        p22 = p22 + r;
        p23 = p23 - r * 0.25f;
        /* right arm, x (mirrored) */
        x = Vb.x - R10.x;
        r = unit(x, seg_value(func_0041A988(body, 0x10))) * 0x1.e28c6ep-3f;
        z1 = R10.z - Vb.z;
        p25 = p25 + r * 0x1.999998p-5f;
        p23 = p23 + r;
        p22 = p22 - r * 0.25f;
        /* right arm, z */
        s = func_0041B9D8(body + 0x3B0);
        s = s + func_0041B9D8(body + 0x450);
        s = s + func_0041B9D8(body + 0x5E0);
        r = unit(z1, s) * -0x1.e28c6ep-2f;
        p23 = p23 + r;
        p22 = p22 - r * 0.25f;
        p23 = p23 * 0.75f + F(self, 0x2A4);
        p22 = p22 * 0.75f + F(self, 0x2A0);
        F(self, 0x44) = p23;
        F(self, 0x40) = p22;
        /* roll the shoulder point about H by p25 (quaternion about x) */
        func_00487950(&q3B0, p25);
        Vb.x = Vb.x - H.x;
        Vb.y = Vb.y - H.y;
        Vb.z = Vb.z - H.z;
        func_00421E50(&rot, &q3B0, &Vb);
        Vb = rot;
        Vb.x = Vb.x + H.x;
        Vb.y = Vb.y + H.y;
        Vb.z = Vb.z + H.z;
    }

    {
        f32 add = 0.0f;
        if (I(self, 0x2D8) != 0)
            add = 0x1.41b2f4p-2f;
        F(self, 0x48) = F(self, 0x2A8) + add;
        F(self, 0x4C) = F(self, 0x2AC) + add;
        I(self, 0xA8) = 0;
    }

    if (I(self, 0xBC) != 0) {
        /* arm twist: the angle at the elbow of each arm (sides shoulder-hand, hand-grip, shoulder-grip) */
        V G = VV(tg, 0x50);
        f32 la, lb, ra, rb, ta, tb;
        f32 d3 = mag(sub(Vb, G));
        la = mag(sub(L10, Vb));
        lb = mag(sub(G, L10));
        func_004233C0(&ta, la, lb, d3);
        ra = mag(sub(R10, Vb));
        rb = mag(sub(G, R10));
        func_004233C0(&tb, ra, rb, d3);
        ta = clamp_(0x1.921fb4p+0f - ta, -0x1.41b2f4p+0f, 0x1.41b2f4p+0f);
        tb = clamp_(0x1.921fb4p+0f - tb, -0x1.41b2f4p+0f, 0x1.41b2f4p+0f);
        func_00487830(&q3C0.x, &L->v20.x, -ta);
        func_00487830(&q3D0.x, &R->v20.x, tb);
        func_00421E50(&tmp, &q3C0, &L30);
        L30 = tmp;
        func_00421E50(&tmp, &q3D0, &R30);
        R30 = tmp;
        VV(out, 0x70) = L->v20;
        VV(out, 0x80) = R->v20;
        VV(out, 0xA0) = R30;
        VV(out, 0x90) = L30;
        F(self, 0x20) = clamp_(L->f40, 0.0f, 1.0f);
        F(self, 0x24) = clamp_(R->f40, 0.0f, 1.0f);
        F(self, 0x18) = clamp_(L->f44, 0.0f, 1.0f);
        F(self, 0x1C) = clamp_(R->f44, 0.0f, 1.0f);
        F(self, 0x28) = L->f48 + F(self, 0x248);
        F(self, 0x2C) = R->f48 + F(self, 0x24C);
    }

    /* transform the points by tg's frame into `out` */
    T.pos = VV(tg, 0);
    func_00421C58(&T.m, tg + 0x10);
    tmp = H;
    tmp.x += F(self, 0x340);
    tmp.y += F(self, 0x344);
    tmp.z += F(self, 0x348);
    func_00418818(&T, (V *)(out + 0x10), &tmp);
    func_00418818(&T, (V *)(out + 0x50), &L10);
    func_00418818(&T, (V *)(out + 0x60), &R10);
    func_00418818(&T, (V *)(out + 0x40), &Vb);
    F(out, 0x40) += F(self, 0x2C0);
    F(out, 0x44) += F(self, 0x2C4);
    F(out, 0x48) += F(self, 0x2C8);
    if (I(self, 0xC0) != 0) {
        func_00418818(&T, (V *)(out + 0x20), &C1);
        func_00418818(&T, (V *)(out + 0x30), &C2);
    }
    {
        Q q = *(Q *)out;
        V a3A0;
        a3A0.x = 0.0f; a3A0.y = sn; a3A0.z = -cs; a3A0.w = 0.0f;
        func_00421E50(&tmp, &q, &a3A0);
        VV(out, 0xB0) = tmp;
    }
    return 1;
}
