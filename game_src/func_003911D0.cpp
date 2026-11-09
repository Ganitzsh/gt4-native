/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native copy of 003911D0 (RaceCarModel's virtual 35: the car's lamp glows into the glow pass) from gt4-decomp's
 * src/race/RaceCarModel/RaceCarModel__virtual_35.cpp (c33fb7a54), with A7 step 2's calibration: once the car's matrix
 * is loaded (vtbl[33]: set 0 is the model's object-to-view), gt4_scene_car_drawn takes where GT4 draws the model as the
 * car's lamp offset along its body's row 0 (docs/modern/CONTRACTS.md a.4, PLAN A7 step 2). Nothing is sent from here;
 * nothing runs while scene records are off. */
#include "types.h"
#include "gt4_gfx.h"
#include "spr.h"

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
    gt4_scene_car_drawn(spr_base(), self);   /* A7 step 2: the lamp offset, from this draw */
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
    func_004A5400();
}
