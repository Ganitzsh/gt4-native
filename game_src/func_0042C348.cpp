/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native correction of the provisional draft drafts/0042c348/0042c348_62befb64197b5ee2.cpp (argument order, argcheck.py orders): func_0042B440 takes its first float third, between its second and third ints, as defined (game_src/func_0042B440.cpp); the draft's order is the same EE call but other stack slots natively. */
struct Elem {
    short *tbl; float f4; float f8; float fC; unsigned short n; unsigned char flag;
};
struct Obj { char pad[8]; char *base; };
extern "C" Elem *func_0042A3D8(void *) throw();
extern "C" int func_0042B440(Obj *, int, float, int, float, float, int) throw();
inline int clamp(int x, int lo, int hi) { if (x < lo) return lo; if (x > hi) return hi; return x; }
extern "C" int func_0042C348(Obj *o, int idx, int x, float t, float fr) {
    Elem *e = func_0042A3D8(o->base + (idx << 5));
    float f2 = e->f4;
    if (e->flag) {
        t = (f2 + (f2 + e->fC)) - t;
    }
    int a = (int)fr;
    int b = a + 1;
    int n1 = e->n - 1;
    short s1 = e->tbl[clamp(a, 0, n1)];
    short s2 = e->tbl[clamp(b, 0, n1)];
    t = (t - f2) + e->f8;
    fr = fr - (float)(int)fr;
    if (s1 == s2) fr = 0.0f;
    if (fr == 0.0f)
        return func_0042B440(o, s1, t, 0, 0.0f, 0.0f, x);
    return func_0042B440(o, s1, t, s2, t, fr, x);
}
