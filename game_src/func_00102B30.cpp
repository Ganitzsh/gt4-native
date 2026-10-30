/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native copy of 00102B30 (matched), a start-up screen: its 16:9-mode x squeeze 0.75 is (4/3) / aspect (gt4_wide_squeeze). (docs/WIDESCREEN.md) */
extern "C" float gt4_wide_squeeze(void);
typedef int s32;
struct VEntry { short delta; short index; void (*fn)(void *, void *, void *); };
struct VEntry0 { short delta; short index; s32 (*fn)(void *); };
struct Vt { char pad[0x20]; VEntry e20; VEntry0 e28; };
struct Sub { Vt *vt; };
struct G { char pad[0x18]; char *p18; };
struct O { char pad[0x9C]; Sub *p9C; s32 pA0; };
extern "C" void func_00102D00(char *);
extern "C" void func_001056A0(void *, void *arg1);
extern "C" int func_00499728(void *);
extern "C" void func_0049CC48(void *);
extern "C" void func_004A1638(s32);
extern "C" void func_004A2808(s32, float);
extern "C" void RaceEntryCar__update(s32);
extern "C" void func_004A53F8(void);
extern "C" void func_004A5400(void);
extern "C" void func_004A5A90(float, float, float, float, float, float);
extern "C" void func_004A7454(void);
extern "C" void func_004A7844(float, float, float);
extern "C" void func_004A7890(float, float, float);
extern "C" void func_004AB040(s32);
extern G *D_00618300;
extern s32 D_006184F0;

extern "C" void func_00102B30(O *self, void *arg1) {
    if (self->pA0 != 0) {
        func_001056A0(arg1, arg1);
        Sub *s = self->p9C;
        VEntry *a = &s->vt->e20;
        void *t = (char *)s + a->delta;
        a->fn(t, self, arg1);
        VEntry0 *b = &s->vt->e28;
        s32 idx = b->fn((char *)s + b->delta);
        if (idx >= 0) {
            G *g = D_00618300;
            char *ent = g->p18 + idx * 0x28;
            RaceEntryCar__update(1);
            func_004A53F8();
            func_004A7454();
            func_004A5A90(-320.0f, 320.0f, -224.0f, 224.0f, -1.0f, 1.0f);
            RaceEntryCar__update(0);
            func_004A53F8();
            func_004A7454();
            func_004AB040(0xF);
            func_004AB040(0x11);
            func_004AB040(5);
            func_004AB040(4);
            func_004A1638(2);
            func_004AB040(8);
            func_004AB040(6);
            func_004A1638(6);
            func_004A2808(0x44, 1.0f);
            func_00499728(g);
            func_0049CC48(ent);
            if (D_006184F0) func_004A7890(gt4_wide_squeeze(), 1.0f, 1.0f);
            func_004A7844(0.0f, -117.0f, 0.0f);
            func_00102D00((char *)self);
            RaceEntryCar__update(1);
            func_004A5400();
            RaceEntryCar__update(0);
            func_004A5400();
        }
    }
}
