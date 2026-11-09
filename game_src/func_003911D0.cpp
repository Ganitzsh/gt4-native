/* compiler: ee-gcc2.96-no-strict-aliasing */
/* A7 step 2, step 0b DEBUG (local, never pushed): the matched decomp source of RaceCarModel virtual 35 (003911D0),
 * logging each lamp element's address and model-space point once per group (gt4_logf), to match step 1a's kSrcCarLamp
 * lights (their id is the element's address) in a capture of the same session. */
#include "types.h"

struct Elem {
    char pad0[0x64];
    u16 flags;
    u16 index;
    f32 scale;
    char pad6C[0x4];
};

struct Group {
    char pad0[0x18];
    s32 count0;
    Elem *elems0;
    s32 count2;
    Elem *elems2;
    s32 count1;
    Elem *elems1;
};

struct Params {
    char pad0[0x1794];
    f32 unk1794;
    f32 unk1798;
    f32 unk179C;
};

struct Info {
    char pad0[0x80];
    Params *params;
};

struct InfoHolder {
    char pad0[0x4];
    Info *info;
};

struct Car {
    char pad0[0x8];
    InfoHolder *holder;
};

struct Sub18 {
    char pad0[0x3C];
    f32 unk3C;
};

struct VEntry {
    short delta;
    short index;
    Group *(*fn)(void *);
};

struct VEntryF {
    short delta;
    short index;
    void (*fn)(void *, f32);
};

struct RaceCarModel {
    char pad0[0x4];
    Car *car;
    VEntry *vtbl;
    char padC[0xC];
    Sub18 unk18;
    char pad58[0x1678 - 0x58];
    f32 arr[2];
    s32 unk1680;
};

struct Vec4 {
    f32 v[4];
};

extern "C" s32 func_003446B8(Car *);
extern "C" s32 func_004515B0(Sub18 *);
extern "C" s32 Automobile__getDrawMode(Car *);
extern "C" void func_004A53F8(void);
extern "C" void func_004A5400(void);
extern "C" Vec4 *func_003917D8(void);
extern "C" f32 func_003916E0(Vec4 *, f32, f32);
extern "C" void func_003B8530(void *, Elem *, f32, s32);

extern "C" void gt4_logf(const char *fmt, ...);
static void dbg_elements(Group *g, Car *car);
extern "C" void RaceCarModel__virtual_35(RaceCarModel *self, void *drawer, s32 arg2, f32 arg3) {
    Car *car;
    s32 hasX;
    Sub18 *sub;
    Group *g;
    Params *params;
    s32 notY;
    s32 n;
    s32 i;

    if (self->unk1680 == 0) {
        return;
    }
    car = self->car;
    sub = &self->unk18;
    hasX = func_003446B8(car) != 0;
    {
        VEntry *e = &self->vtbl[10];
        g = e->fn((char *)self + e->delta);
    }
    params = car->holder->info->params;
    notY = func_004515B0(sub) ^ 1;
    if (Automobile__getDrawMode(car) < 2) {
        return;
    }
    func_004A53F8();
    {
        VEntryF *e = (VEntryF *)&self->vtbl[33];
        e->fn((char *)self + e->delta, arg3);
    }
    {
        n = g->count0;
        for (i = 0; i < n; i++) {
            Elem *el = &g->elems0[i];
            u16 idx = el->index;
            if (idx != 0) {
                Vec4 *t = func_003917D8();
                f32 s = func_003916E0(&t[idx - 1], self->arr[idx - 1], el->scale);
                func_003B8530(drawer, el, s, 1);
            } else {
                func_003B8530(drawer, el, params->unk1794, 1);
            }
        }
    }
    if (arg2 != 0) {
        f32 k = sub->unk3C;
        if (0.0f < k) {
            n = g->count1;
            for (i = 0; i < n; i++) {
                Elem *el = &g->elems1[i];
                f32 s = params->unk1798;
                if (notY && (el->flags & 2)) {
                    continue;
                }
                if (el->flags & 1) {
                    func_003B8530(drawer, el, k * s, 1);
                } else if (hasX) {
                    func_003B8530(drawer, el, s, 1);
                }
            }
        }
        n = g->count2;
        for (i = 0; i < n; i++) {
            Elem *el = &g->elems2[i];
            if (notY && (el->flags & 2)) {
                continue;
            }
            func_003B8530(drawer, el, params->unk179C, 0);
        }
    }
    dbg_elements(g, car);
    func_004A5400();
}

/* (after the exported function: game_cc binds the address to the file's first .text function) */
static void dbg_elements(Group *g, Car *car) {
    static void *seen[64];
    static int nseen = 0;
    for (int i = 0; i < nseen; ++i)
        if (seen[i] == (void *)g) return;
    if (nseen >= 64) return;
    seen[nseen++] = (void *)g;
    struct { Elem *e; int n; int group; } groups[3] = {{g->elems0, g->count0, 0}, {g->elems1, g->count1, 1},
                                                        {g->elems2, g->count2, 2}};
    for (int k = 0; k < 3; ++k)
        for (int i = 0; i < groups[k].n && i < 24; ++i) {
            const Elem *e = &groups[k].e[i];
            const float *f = (const float *)((const char *)e + 0x30);
            gt4_logf("a7s0b element %08x car %p group %d index %d flags %04x model %.4f %.4f %.4f beam %.4f %.4f %.4f",
                     (unsigned)(unsigned long)e, (void *)car, groups[k].group, i, e->flags, f[0], f[1], f[2], f[6], f[7],
                     f[8]);
        }
}
