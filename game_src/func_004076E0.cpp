/* compiler: ee-gcc2.96-stl */
/* Native correction of the provisional draft drafts/004076e0/004076e0_05f69308862194c4.cpp (argument widths, argcheck.py width/retwidth): func_00449E10 takes `s64 id` and func_004454C0 returns s64 (a 64-bit database id); the draft declared both int, so natively the callee read the id's high half from the next stack slot. */
typedef unsigned int u32;
typedef long long s64;

struct Set {
    int w0;
    char pad4[0x40];
    int cnt;
};

struct Obj {
    Set a;
    u32 bitsA;
    Set b;
    u32 bitsB;
    s64 q98;
    s64 qA0;
    int flagA8;
    char padAC[4];
    char cB0[1];
};

struct Src {
    char pad[0x60];
    s64 q60;
    s64 q68;
};

extern "C" int func_004462E0(void *);
extern "C" int func_00446320(void *);
extern "C" int func_00441300(int);
extern "C" long func_004454C0(void *);
extern "C" int func_00449E10(void *, long);

static inline void insert(Set *set, int i, int r, int expected) {
    char *base = (char *)set + 8;
    u32 *w = (u32 *)(base + (u32)((i >> 5) * 4 + 0x40));
    u32 m = 1 << (i & 31);
    if (r == expected) {
        u32 v = *w;
        if (!(v & m)) {
            *w = v | m;
            set->cnt++;
        }
    }
}

extern "C" void func_004076E0(Obj *self, Src *src) {
    int i = 0;
    int *ca = &self->a.cnt;
    self->a.w0 = 0;
    *ca = 0;
    self->bitsA = 0;
    int ea = func_004462E0(src);
    Set *b = &self->b;
    self->bitsB = 0;
    b->w0 = 0;
    b->cnt = 0;
    int eb = func_00446320(src);
    int ra = func_00441300(ea);
    for (i = 0; i < 13; i++) {
        int r = func_00441300(i);
        insert(&self->a, i, r, ra);
    }
    int rb = func_00441300(eb);
    for (i = 0; i < 13; i++) {
        int r = func_00441300(i);
        insert(&self->b, i, r, rb);
    }
    if (self->flagA8 == 0) {
        func_00449E10(self->cB0, func_004454C0(src));
        self->flagA8 = 1;
        self->q98 = src->q60;
        self->qA0 = src->q68;
    }
}
