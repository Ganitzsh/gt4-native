struct E { unsigned char flag; char pad1[3]; float a; float b; float c; short sh; char pad12[2]; float f; float rad; char pad1c[8]; float ang; };
struct O { int a; int cnt; int b, c; };
struct S { int first; };
extern "C" float func_0046A0B0(struct S *s);
extern "C" void func_00469F08(struct O *o, struct S *s, int n);
extern "C" struct E *func_00469FC8(struct O *o, int i);
extern "C" int func_0046B310(struct O *, int, int *, int *, float);
extern "C" float func_0057D118(float a, float b);
extern "C" float func_0057D1F0(float a);
extern "C" float func_0057D2B8(float a);

extern "C" int func_0046B480(struct S *s, int key, int start, float *ox, float *oy, float *oz, int *p1, int *p2, float t) {
    int i0, i1;
    struct O o;
    float f, fa, fb, frac;
    int r;
    struct E *e0, *e1;
    float a, b, c, d;

    f = func_0046A0B0(s);
    frac = 0.0f;
    i0 = 0;
    i1 = 0;
    if (p1) *p1 = 0;
    if (p2) *p2 = 0;
    if (t < 0.0f) t += f;
    else if (f <= t) t -= f;
    func_00469F08(&o, s, key);
    r = func_0046B310(&o, start, &i0, &i1, t);
    e1 = func_00469FC8(&o, i1);
    e0 = func_00469FC8(&o, i0);
    switch (r) {
    case 1: {
        float g = e1->f;
        fa = (t + f) - g;
        fb = (e0->f + f) - g;
        break;
    }
    case 2: {
        float g = e1->f;
        fa = t - g;
        fb = (e0->f + f) - g;
        break;
    }
    case 3: {
        float g = e1->f;
        fa = t - g;
        fb = e0->f - g;
        break;
    }
    default:
        return -1;
    }
    frac = fa / fb;
    a = e1->a;
    b = e1->b;
    c = e0->a;
    d = e0->b;
    if (e1->flag == 0) {
        *ox = frac * (c - a) + a;
        *oy = frac * (d - b) + b;
    } else {
        float ang = e1->ang;
        float ns = -func_0057D2B8(ang);
        float cs = func_0057D1F0(ang);
        float rad = e1->rad;
        float cx = a - cs * rad;
        float cy = b + ns * rad;
        float v1 = func_0057D118(cx - a, b - cy);
        float v2 = func_0057D118(cx - c, d - cy);
        float mid;
        if (rad > 0.0f) {
            if (v2 < v1) v2 += 6.2831850052f;
        } else {
            rad = -rad;
            if (v1 < v2) v1 += 6.2831850052f;
        }
        mid = v1 + frac * (v2 - v1);
        *ox = cx - func_0057D2B8(mid) * rad;
        *oy = cy + func_0057D1F0(mid) * rad;
    }
    {
        float z0 = e1->c;
        *oz = frac * (e0->c - z0) + z0;
    }
    if (p1) *p1 = e1->sh;
    if (p2) *p2 = e0->sh;
    return i1;
}
/* native correction of src/engine/unit_0046A050/func_0046B480.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_0046B310 */
