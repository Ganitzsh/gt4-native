/* compiler: ee-gcc2.96-no-strict-aliasing */
typedef int s32; typedef float f32; typedef unsigned short u16;

struct Q { char pad0[0x10]; f32 f10, f14, f18; char pad1C[4]; f32 f20, f24; };
struct S {
    char pad0[8];
    f32 f8;
    char padC[8];
    f32 f14;
    char pad18[0x38 - 0x18];
    f32 f38;
    char pad3C[0x44C - 0x3C];
    f32 f44C;
    char pad450[0x4BA - 0x450];
    u16 h4BA;
    char pad4BC[0x54C - 0x4BC];
    f32 f54C;
    char pad550[8];
    f32 f558;
    char pad55C[0x568 - 0x55C];
    f32 f568;
    f32 f56C;
    char pad570[0x580 - 0x570];
    f32 f580, f584;
};
struct Obj { char pad0[0x10]; Q *q; char pad14[0x104 - 0x14]; S s; };

extern "C" f32 func_0036A298(s32, s32 *, f32);
extern "C" f32 func_0057D118(f32, f32);

extern "C" s32 func_0036A318(Obj *self, s32 *out, f32 ang) {
    S *s = &self->s;
    u16 h = s->h4BA;
    s32 flag = 0;
    f32 a = func_0036A298(h, &flag, ang);
    f32 b = func_0036A298(s->h4BA + 0xF, &flag, ang);
    if (flag == 0) {
        *out = 0;
    } else {
        *out = 1;
    }
    Q *q = self->q;
    s32 hit = 0;
    f32 k = s->f14;
    f32 base = s->f38;
    f32 cur = s->f8;
    f32 lo = base + a;
    f32 hi = base + b;
    f32 x = cur + k * q->f10;
    f32 y = cur - k * q->f14;
    if (x < lo) {
        x = lo;
        hit = 1;
    }
    if (y < hi) {
        y = hi;
        hit = 1;
    }
    s32 ret = 0;
    if (hit) {
        if (*out == 2) {
            s->f568 = 0.0f;
            s->f558 = 0.0f;
            s->f8 = base + ang;
        } else {
            s->f8 = x * q->f24 + y * q->f20;
            s->f568 = func_0057D118(x - y, q->f18);
            s->f558 = (s->f8 - cur) * s->f44C;
        }
        s->f56C = 0;
        ret = 1;
        f32 z = s->f56C;
        s->f584 = z;
        s->f580 = z;
        s->f54C = z;
    }
    return ret;
}
/* native correction of src/race/unit_00364818/func_0036A318.cpp: calls func_0057D118(float y, float x) with its two floats only; the q pointer in $a0 is not a parameter of the callee. */
