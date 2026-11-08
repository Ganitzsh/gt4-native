/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native copy of 0038B5A0 (RaceBasic::render_panel, which RaceSolitaire's calls and RaceSplitBattleBase's reaches per
 * player) from gt4-decomp's src/race/unit_0038B0E0/RaceBasic__render_panel.cpp, the source the native game compiled until
 * now, with the HUD's markers (docs/modern/CONTRACTS.md a.4, a.5 5): kMarkHudBegin at its start, kMarkHudEnd at its end.
 * Nothing is sent while scene records are off. */
#include "gt4_gfx.h"
#include "spr.h"
typedef int s32;
typedef short s16;

struct VE { s16 delta; s16 idx; void *fn; };
struct A { VE *vt; };
struct B { char pad[0xC]; VE *vt; };
struct C { char pad[8]; VE *vt; };

struct RaceBasicLayout {
    char pad0[0x6C]; void *u6C;
    char pad70[0xCD8 - 0x70]; s32 cd8;
    char padCDC[0xE40C - 0xCDC];
    void *unkE40C;
    char pad410[4];
    void *unkE414;
    C *unkE418;
    B *unkE41C;
    A *unkE420;
    void *unkE424;
};

extern "C" void RacePS2Base__render_panel(void *, s32);
extern "C" void func_001056A0(s32);
extern "C" void func_003DA150(void *);
extern "C" void func_003DD690(void *);
extern "C" void func_003BAD08(void *, s32);

extern "C" void RaceBasic__render_panel(RaceBasicLayout *self, s32 arg) {
    gt4_scene_marker(spr_base(), 3);   /* kMarkHudBegin */
    RacePS2Base__render_panel(self, arg);
    void *u = self->u6C;
    A *a = self->unkE420;
    if (a) {
        VE *e = &a->vt[3];
        ((void (*)(void *, s32))e->fn)((char *)a + e->delta, arg);
    }
    func_001056A0(arg);
    if (self->unkE424) func_003DA150(self->unkE424);
    B *b = self->unkE41C;
    if (b) {
        VE *e = &b->vt[5];
        ((void (*)(void *, s32, void *))e->fn)((char *)b + e->delta, arg, u);
    }
    if (self->unkE40C) func_003DD690(self->unkE40C);
    if (self->unkE414) func_003BAD08(self->unkE414, arg);
    if (self->cd8) {
        C *c = self->unkE418;
        VE *e = &c->vt[5];
        ((void (*)(void *, s32, void *))e->fn)((char *)c + e->delta, arg, u);
    }
    gt4_scene_marker(spr_base(), 4);   /* kMarkHudEnd */
}
