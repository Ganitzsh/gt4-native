/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native copy of 0033EAB8 (RacePS2Base's scene, from render_world) from gt4-decomp's src/race/RaceBGMPS2_RacePS2Base/func_0033EAB8.cpp, the source the native game compiled until now, with the main
 * view's scene records (docs/modern/CONTRACTS.md a.4, spike R1): its ViewBegin once the camera is loaded (set 1 by
 * its +0x10, set 0 by its +0x18), its ViewEnd before the rear-view mirror, which is drawn after every main-view
 * instance (render_back_mirror 00341E50 has its own view). (The glow pass's lamps go as Lights from 0033F2E8, where
 * they are drawn.) Nothing is sent while scene records are off. */
#include "gt4_gfx.h"
#include "spr.h"
typedef int s32;
typedef short s16;
typedef float f32;
struct V { s16 d, i; void *fn; };
#define P(p,o) (*(char **)((char *)(p)+(o)))
#define I(p,o) (*(s32 *)((char *)(p)+(o)))
#define F(p,o) (*(f32 *)((char *)(p)+(o)))
typedef char *(*Get)(char *, int, f32);
typedef s32 (*Test)(char *);
typedef f32 (*FGet)(char *);
typedef void (*Fn)(char *);
typedef void (*FnF)(char *, f32);
typedef void (*FnP)(char *, void *);
typedef void (*FnPF)(char *, void *, f32);
typedef void (*FnPI)(char *, void *, s32);
typedef void (*FnPPIF)(char *, void *, void *, s32, f32);
typedef void (*FnPPPF)(char *, void *, void *, void *, f32);
typedef void (*FnPIIIF)(char *, void *, s32, s32, s32, f32);
typedef void (*FnIF)(char *, s32, f32);
struct Rect { f32 x, y, w, h, scale; };
extern s32 D_00621D24;
extern "C" {
void func_001056A0(void *);
void func_00105708(void *);
void func_001057F8(void *);
s32 func_00105818(void *);
void func_001079C8(s32, s32);
void func_0033DC88(char *, void *, s32, f32) throw();
void func_0033EA30(char *, void *) throw();
void func_00341D88(char *, void *, s32);
void func_00342470(void *);
void func_00342580(void *, f32, f32);
void func_00396B80(char *, char *, s32, f32, f32);
void func_00396D40(char *);
void func_00397CE0(char *);
void func_00397D48(char *);
void func_00397DF8(char *);
void func_003985A0(char *, char *);
void func_00398650(char *, char *);
void func_003B7B68(char *, s32);
void func_003B80E8(char *, void *);
void func_003B8848(char *);
void func_003C2838(char *);
s32 Pitmen__isNotRenderOthersPeriod(char *, s32);
void func_003E50E0(char *);
void func_003E5338(char *);
void func_00456918(f32);
void func_004A1638(s32);
void func_004A3078(s32);
void func_004A3088(f32);
void func_004A3230(s32);
void RaceEntryCar__update(s32);
void func_004A53F8();
void func_004A5400();
void func_004A7454();
void func_004A7698(char *);
}
static inline bool hasFlag(char *obj, s32 mask) { return (I(obj, 0x10) & mask) != 0; }
static inline void setFlare(s32 on) { D_00621D24 = on; }
static inline V *entry(char *p, int off) { return (V *)(P(p, 0) + off); }
static inline V *method(char *self, int off) { return (V *)(P(self, 0x64) + off); }

extern "C" void func_0033EAB8(char *self, void *buf, void *cur, void *aux, f32 t, s32 car) {
    f32 one = 1.0f;
    char *course = P(P(P(self, 0x6C), 0x80), 4);
    char *scene = P(course, 0x80);
    V *v = method(self, 0xD0);
    char *cam = ((Get)v->fn)(self + v->d, 0, t);
    func_001056A0(buf);
    func_00105708(cur);
    func_004A3078(I(self, 0xCF64));
    func_004A3088(one);
    func_004A3230(3);
    func_0033EA30(self, buf);
    func_001056A0(buf);
    RaceEntryCar__update(1);
    func_004A7454();
    F(cam, 0xA08) = F(scene, 0x20);
    { V *e = entry(cam, 0x10); ((FnF)e->fn)(cam + e->d, t); }
    func_004A7698(self + 0xE36C);
    func_00456918(one);
    RaceEntryCar__update(0);
    func_004A7454();
    { V *e = entry(cam, 0x18); ((FnF)e->fn)(cam + e->d, t); }
    gt4_scene_main_camera(spr_base(), cam);      /* A3b: its mode fields and cut pulses, for this view's kViewCut */
    gt4_scene_view_begin(spr_base(), 1, 4, 0);   /* kViewMain, kViewCameraKnown: the camera is set 0 now */
    func_00397CE0(scene);
    if (I(self, 0xE340)) {
        func_00396B80(course, P(course, 0x54), -1, one, 1000.0f);
        func_00398650(scene, self + 0xDA0);
    }
    func_00397DF8(scene);
    if (I(self, 0xE33C)) {
        func_00396D40(course);
        func_004A3230(2);
        func_00342470(buf);
        { V *e = method(self, 0x370); ((FnPI)e->fn)(self + e->d, cam, I(self, 0xE334)); }
        if (I(self, 0xE34C)) func_001079C8(0, 0);
        gt4_scene_reflection(spr_base(), 1);   /* render_reflection (vtable slot 110): kInstReflection, A6 */
        { V *e = method(self, 0x378); ((FnPPIF)e->fn)(self + e->d, buf, aux, I(self, 0xE334), t); }
        gt4_scene_reflection(spr_base(), 0);
    }
    func_00397D48(scene);
    func_00342580(buf, F(scene, 0x3C), 1.0f);
    if (I(self, 0xE33C)) {
        func_004A53F8();
        if (I(P(self, 0x6C), 0xE0) == 0) {
            if (hasFlag(scene, 0x800)) {
                setFlare(1);
                func_003E50E0(scene + 0xAD0);
            } else {
                setFlare(0);
            }
            func_003E5338(self + 0x35E8);
        }
        func_004A5400();
    }
    { V *e = method(self, 0x468);
      if (((Test)e->fn)(self + e->d)) {
          func_004A1638(5);
          V *e2 = method(self, 0x3A0); ((FnP)e2->fn)(self + e2->d, aux);
      } }
    s32 index = *(s16 *)(cam + I(cam, 0x9BC) * 0x19C + 0x1C);
    if (I(self, 0xE338)) {
        func_004A53F8();
        s32 target = -1;
        func_004A1638(5);
        if (Pitmen__isNotRenderOthersPeriod(self + 0x3628, index)) target = index;
        func_0033DC88(self, aux, index, t);
        { V *e = method(self, 0x3D8); ((FnPPPF)e->fn)(self + e->d, buf, cur, aux, t); }
        { V *e = method(self, 0x380); ((FnPIIIF)e->fn)(self + e->d, aux, car, car < 0, target, t); }
        { V *e = method(self, 0x3E0); ((FnPPPF)e->fn)(self + e->d, buf, cur, aux, t); }
        func_004A5400();
    }
    func_004A53F8();
    { V *e = entry(cam, 0x1C8); ((Fn)e->fn)(cam + e->d); }
    func_004A5400();
    { V *e = method(self, 0x498); ((FnPF)e->fn)(self + e->d, aux, 0.0f); }
    if (I(self, 0xE33C)) { V *e = method(self, 0x3A8); ((FnP)e->fn)(self + e->d, cam); }
    { V *e = method(self, 0x3B8); F(self, 0xE328) = ((FGet)e->fn)(self + e->d); }
    V *e3 = method(self, 0x3B0);
    s32 on = ((Test)e3->fn)(self + e3->d);
    s32 flag = 0;
    if (F(self, 0xE328) <= 0.999f) flag = on != 0;
    I(self, 0xE324) = flag;
    gt4_scene_view_end(spr_base());   /* every main-view instance is drawn; the mirror is a view of its own */
    if (on) {
        s32 w = I(buf, 8);
        s32 h = func_00105818(buf);
        s32 rh = h * 58 / 448;
        s32 rw = w * 260 / 640;
        s32 cy = h * 49 / 448;
        Rect *r = (Rect *)(self + 0xE2E0);
        r->scale = 1.3f;
        r->w = (f32)rw;
        r->h = (f32)rh;
        r->x = (f32)(w / 2 - rw / 2);
        r->y = (f32)(cy - rh / 2);
        if (I(self, 0xE324)) {
            if (0.5f < F(self, 0xE328)) func_00341D88(self, buf, 0);
        } else {
            V *e = method(self, 0x3C0); ((FnPI)e->fn)(self + e->d, buf, 0);
        }
    }
    if (I(self, 0xE344)) {
        char *rb = self + 0xDA0;
        func_003B7B68(rb, 0);
        func_003B80E8(rb, buf);
        func_003985A0(scene, rb);
        { bool on = hasFlag(scene, 0x8000); V *e = method(self, 0x3F0); ((FnIF)e->fn)(self + e->d, on, t); }
        if (hasFlag(scene, 0x200000)) { V *e = method(self, 0x3F8); ((Fn)e->fn)(self + e->d); }
        gt4_scene_marker(spr_base(), 7);   /* kMarkFlareBegin: the flare's read (A5b) */
        func_003B8848(rb);
        gt4_scene_marker(spr_base(), 8);   /* kMarkFlareEnd */
        if (!Pitmen__isNotRenderOthersPeriod(self + 0x3628, index)) func_003C2838(self + 0x3238);
    }
    /* A7 step 2: every car's lamps as Lights of the main view, after the glow pass (method 0x3F0 runs each car's
     * virtual 35, which draws with this frame's brake level and calibrates the lamp offset) */
    gt4_scene_car_lamps(spr_base(), 0, self);
    { V *e = method(self, 0x3E8); ((FnPPPF)e->fn)(self + e->d, buf, cur, aux, t); }
    func_001057F8(buf);
}
