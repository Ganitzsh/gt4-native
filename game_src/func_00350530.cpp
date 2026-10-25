/* compiler: ee-gcc2.96-no-strict-aliasing */
typedef unsigned char u8;
struct VEntry { short delta; short pad; void (*fn)(void *, void *, int, int, float); };
struct VTab { char p[0x190]; VEntry e; };
struct Mgr { char p[0x10140]; VTab *vt; };
struct B { char p0[0x467 - 0x104 + 0x104 - 0x104]; };
struct S { char p0[4]; Mgr *mgr; };
extern "C" void func_0035CF40(void *);
extern "C" void func_0035CF88(void *, int);
extern "C" void func_0035D0C0(void *);
extern "C" int func_00350328(void *, int, int, float) throw();
static inline void update(u8 *b) {
    if (b[0x467] == 0) {
        if (!(b[0x630] & 2)) {
            u8 c = b[0x62F];
            if (c < 100) b[0x62F] = c + 1;
        }
        b[0x630] |= 2;
        b[0x631] = 60;
    }
}
extern "C" int func_00350530(S *self, int k, int n, float f)
{
    switch (k) {
    case 0: func_0035CF40(self); break;
    case 1: func_0035CF88(self, n); break;
    case 2: func_0035D0C0(self); break;
    }
    update((u8 *)self + 0x104);
    Mgr *m = self->mgr;
    VEntry *e = &m->vt->e;
    void *thisp = (char *)m + e->delta;
    e->fn(thisp, self, k, n, f);
    return func_00350328(self, k, n, f);
}
/* native correction of src/race/unit_0034E9A0/func_00350530.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_00350328 */
