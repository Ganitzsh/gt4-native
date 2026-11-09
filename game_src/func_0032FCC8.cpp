/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native copy of 0032FCC8 (RaceSplitBattleBase::render: 2 Player Battle's split screen) from gt4-decomp's
 * src/race/RaceSplitBattle/RaceSplitBattleBase__render.cpp, with A12's scene records (PLAN A12, CONTRACTS a.4): the
 * frame opens before the halves (kFrameLampsOn from the course scene's flags, as 0033DEA8's copy), each half is
 * told to the runtime with its rectangle before its draw (vt + 0x368, render_world_split, whose copy sends the
 * half's views), and A5a's HUD markers go around each half's panel (vt + 0x358). Outside split mode the normal
 * render_world (vt + 0x348) opens its own frame. Nothing is sent while scene records are off. */
struct E { short delta, index; void *fn; };
struct Params { float x, y, w, h, scale; };
struct Obj {
    char p0[0x64]; E *vt; int p68; void *ctx;
    char p70[0xCC8 - 0x70]; unsigned mode; char pcc[12]; int flag;
    char pcd[0xE2E0 - 0xCDC]; Params params;
    char pe[0xE418 - 0xE2F4]; char *overlay; char *other;
};
struct View { char pad[0x18]; int w, h; char rest[0x20]; };
struct Game { char pad[0x39DF4]; int flag; };
#include "gt4_gfx.h"
#include "spr.h"
extern "C" {
extern Game *D_00622F4C;
extern int D_006184F0;
void func_00105250(void *);
void func_00105280(void *, int);
void *func_00105378(void *, void *);
void func_001054C0(void *, int, int, int, int);
void func_001056A0(void *);
int func_00105918(void *);
void func_00372080(void *, int);
void func_004A3078(int);
void func_004A3230(int);
void func_004A6140(int, int, int, int);
void func_004AA168(int);
}
#define reset(o) ({ E *e = o->vt + 0x3D0 / 8; ((void (*)(void *))e->fn)((char *)o + e->delta); })
#define get(o) ({ E *e = o->vt + 0xD0 / 8; ((void *(*)(void *, int))e->fn)((char *)o + e->delta, 0); })
#define panel(o) ({ E *e = (o)->vt + 0x4C8 / 8; ((int *(*)(void *))e->fn)((char *)(o) + e->delta); })
#define draw(o, v, n, f) ({ E *e = o->vt + 0x368 / 8; ((void (*)(void *, void *, int, float))e->fn)((char *)o + e->delta, v, n, f); })
#define draw2(o, v) ({ E *e = o->vt + 0x358 / 8; ((void (*)(void *, void *))e->fn)((char *)o + e->delta, v); })

static inline void setparams(Obj *o, float scale, float x, float y, float w, float h) {
    Params *p = &o->params;
    p->x = x;
    p->y = y;
    p->w = w;
    p->h = h;
    p->scale = scale;
}

static inline void rect(int x0, int y0, int x1, int y1) {
    func_004A6140(x0, y0, x1, y1);
}

extern "C" void RaceSplitBattleBase__render(Obj *o, View *v) {
    void *ctx = o->ctx;
    if (o->mode < 2 || D_00622F4C->flag) {
        {   /* A12: the frame, before the halves (the course scene's 0x8000 is GT4's car-lamp switch) */
            char *self = (char *)o;
            char *scene = *(char **)(*(char **)(*(char **)(*(char **)(self + 0x6C) + 0x80) + 4) + 0x80);
            gt4_scene_frame_begin(spr_base(), (*(int *)(scene + 0x10) & 0x8000) ? 8u : 0u);
        }
        int w = v->w, h = v->h;
        char local[0x40];
        if (D_006184F0 == 0) {
            h >>= 1;
            func_00105250(local);
            func_00105378(local, v);
            reset(o);
            func_00372080(get(o), 0);
            panel(o)[1] = 0;
            func_001054C0(local, 0, 0, w, h);
            gt4_scene_split_half(spr_base(), 0, 0, 0, w, h);   /* A12: the top half */
            Params *p = &o->params;
            float scale = 2.0f;
            p->x = 200.0f;
            p->y = 16.0f;
            p->w = 240.0f;
            p->h = 40.0f;
            p->scale = scale;
            draw(o, local, 1, 0x1.333332p+0f);
            gt4_scene_marker(spr_base(), 3);   /* kMarkHudBegin: the half's panel */
            draw2(o, local);
            gt4_scene_marker(spr_base(), 4);   /* kMarkHudEnd */
            func_00372080(get(o), 1);
            panel(o)[1] = 1;
            func_001054C0(local, 0, h, w, h);
            gt4_scene_split_half(spr_base(), 1, 0, h, w, h);   /* A12: the bottom half */
            p->x = 200.0f;
            p->y = 8.0f;
            p->w = 240.0f;
            p->h = 40.0f;
            p->scale = scale;
            draw(o, local, 1, 0x1.333332p+0f);
            gt4_scene_marker(spr_base(), 3);   /* kMarkHudBegin */
            draw2(o, local);
            gt4_scene_marker(spr_base(), 4);   /* kMarkHudEnd */
            gt4_scene_split_half(spr_base(), -1, 0, 0, 0, 0);
            func_001056A0(v);
            func_004AA168(0x80000000);
            rect(0, (func_00105918(v) >> 1) - 1, v->w, (func_00105918(v) >> 1) + 1);
            func_00105280(local, 2);
        } else {
            w >>= 1;
            func_001056A0(v);
            func_004A3078(0);
            func_004A3230(1);
            func_00105250(local);
            func_00105378(local, v);
            setparams(o, 1.3f, 90.0f, 41.0f, 140.0f, 46.0f);
            reset(o);
            func_00372080(get(o), 0);
            panel(o)[1] = 0;
            func_001054C0(local, 0, 0, w, h);
            gt4_scene_split_half(spr_base(), 0, 0, 0, w, h);   /* A12: the left half */
            draw(o, local, 1, 1.0f);
            func_001054C0(local, 0, 0, w, h);
            gt4_scene_marker(spr_base(), 3);   /* kMarkHudBegin */
            draw2(o, local);
            gt4_scene_marker(spr_base(), 4);   /* kMarkHudEnd */
            func_00372080(get(o), 1);
            panel(o)[1] = 1;
            func_001054C0(local, w, 0, w, h);
            gt4_scene_split_half(spr_base(), 1, w, 0, w, h);   /* A12: the right half */
            draw(o, local, 1, 1.0f);
            func_001054C0(local, w, 0, w, h);
            gt4_scene_marker(spr_base(), 3);   /* kMarkHudBegin */
            draw2(o, local);
            gt4_scene_marker(spr_base(), 4);   /* kMarkHudEnd */
            gt4_scene_split_half(spr_base(), -1, 0, 0, 0, 0);
            func_001056A0(v);
            func_004AA168(0x80000000);
            rect((v->w >> 1) - 1, 0, (v->w >> 1) + 1, func_00105918(v));
            func_00105280(local, 2);
        }
    } else {
        func_00372080(get(o), 0);
        panel(o)[1] = 0;
        E *e = o->vt + 0x348 / 8;
        ((void (*)(void *, void *, float))e->fn)((char *)o + e->delta, v, 0.0f);
        draw2(o, v);
    }
    char *other = o->other;
    if (other) {
        E *e = *(E **)(other + 12) + 5;
        ((void (*)(void *, void *, void *))e->fn)(other + e->delta, v, ctx);
    }
    if (o->flag) {
        char *overlay = o->overlay;
        E *e = *(E **)(overlay + 8) + 5;
        ((void (*)(void *, void *, void *))e->fn)(overlay + e->delta, v, ctx);
    }
    E *e = o->vt + 0x360 / 8;
    ((void (*)(void *, void *))e->fn)((char *)o + e->delta, v);
}
