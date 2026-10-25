/* compiler: ee-gcc2.96-no-strict-aliasing */
typedef int s32;
typedef float f32;

struct Rect { f32 x, y, w, h; };
struct Rec { char pad[0x1D0]; s32 f1D0; };

struct Obj {
    char pad0[0x2CC];
    f32 f2CC;
    f32 f2D0;
    f32 f2D4;
    f32 f2D8;
    f32 f2DC;
    f32 f2E0;
    f32 f2E4;
    f32 f2E8;
    f32 f2EC;
    f32 f2F0;
    f32 f2F4;
};

struct Font {
    char pad0[0x1C];
    f32 f1C;
    f32 f20;
    char pad24[0x50 - 0x24];
    s32 f50;
    char pad54[0x68 - 0x54];
    s32 f68;
    s32 f6C;
    char pad70[4];
    s32 f74;
};

struct Target {
    s32 f0;
    s32 f4;
    char pad8[8];
    s32 f10;
    s32 f14;
    s32 f18;
    s32 f1C;
    char pad20[0x20];
    Target() __asm__("func_00105250");
    ~Target() __asm__("func_00105280");
    void setSize(s32 w, s32 h) __asm__("func_00105460");
    void setFormat(s32 fmt) __asm__("func_001054A0");
    void create() __asm__("func_00105528");
    void bind() __asm__("func_001056A0");
    void finish() __asm__("func_00105728");
};

extern Font *PDISTD__global_font_manager;

extern "C" void func_00121340(Obj *, f32);
extern "C" void func_00121590(Obj *, s32, f32);
extern "C" void func_00121688(Obj *, f32, s32, s32);
extern "C" void func_00121C00(Obj *, s32, f32, f32);
extern "C" void func_00121FF0(Obj *, s32, s32, f32);
extern "C" void func_001226C8(Obj *, s32, f32);
extern "C" void func_00122830(Obj *, f32);
extern "C" void func_00122A50(Obj *, s32, f32);
extern "C" Rec *func_00123500(s32);
extern "C" s32 func_00123640(Obj *, s32);
extern "C" s32 func_00123660(Obj *, s32);
extern "C" s32 func_001236A0(Obj *, s32);
extern "C" void func_004906F8(Font *, s32);
extern "C" void func_00490700(Font *, s32, s32, s32, s32);
extern "C" void func_004A1638(s32);
extern "C" void func_004A2808(s32, f32);
extern "C" void func_004A3078(s32);
extern "C" void func_004A3230(s32);
extern "C" void func_004A4550(s32, s32);
extern "C" void RaceEntryCar__update(s32);
extern "C" void func_004A53F8(void);
extern "C" void func_004A5400(void);
extern "C" void pglPushAttrib(s32);
extern "C" void func_004A5688(void);
extern "C" void func_004A5A90(f32, f32, f32, f32, f32, f32);
extern "C" void func_004A6668(f32, f32, f32, f32, f32, f32, f32, f32);
extern "C" void func_004A7454(void);
extern "C" void func_004AB040(s32);
extern "C" void func_004AB2E8(f32, f32, f32);

static inline void resetBox(Font *f) {
    func_00490700(f, 0, 0, (s32)f->f1C, (s32)f->f20);
}

extern "C" void func_00120CF0(Obj *self, s32 arg1, Rect *rc) {
    s32 w = (s32)(rc->w + 0.5f);
    s32 h = (s32)(rc->h + 0.5f);
    f32 fw = (f32)w;
    f32 fh = (f32)h;
    self->f2EC = fw;
    self->f2F0 = fh;
    self->f2D4 = self->f2CC - fw * 0.5f / self->f2E4;
    self->f2DC = self->f2D4 + fw / self->f2E4;
    self->f2D8 = self->f2D0;
    self->f2E0 = self->f2D0 + fh;
    self->f2F4 = self->f2D0 + fh;
    Target rt;
    rt.setSize(w, h);
    rt.f4 = 0;
    rt.setFormat(0);
    rt.create();
    pglPushAttrib(0x887);
    RaceEntryCar__update(1);
    func_004A53F8();
    RaceEntryCar__update(0);
    func_004A53F8();
    s32 oldW;
    s32 oldH;
    s32 old74;
    {
        Font *f = PDISTD__global_font_manager;
        oldW = (s32)f->f1C;
        oldH = (s32)f->f20;
        f->f1C = fw;
        f->f20 = fh;
        old74 = f->f74;
    }
    resetBox(PDISTD__global_font_manager);
    func_004906F8(PDISTD__global_font_manager, 0);
    PDISTD__global_font_manager->f50 = 0;
    PDISTD__global_font_manager->f68 = 0;
    PDISTD__global_font_manager->f74 = 1;
    PDISTD__global_font_manager->f6C = 1;
    func_004AB040(0x12);
    rt.bind();
    func_004A3078(0);
    func_004A3230(1);
    func_004AB040(5);
    func_004AB040(4);
    func_004AB040(9);
    func_004AB040(6);
    func_004AB040(3);
    func_004AB040(0xF);
    RaceEntryCar__update(1);
    func_004A7454();
    func_004A5A90(self->f2D4, self->f2DC, self->f2D8, self->f2E0, -100.0f, 100.0f);
    RaceEntryCar__update(0);
    func_004A7454();
    s32 n0 = 0;
    if (func_00123500(0)->f1D0 == 0) {
        n0 = func_001236A0(self, 0) != 0;
    }
    s32 n1 = 0;
    if (func_00123500(1)->f1D0 == 0) {
        n1 = func_001236A0(self, 1) != 0;
    }
    s32 count = n0 + n1;
    s32 a = func_00123640(self, 0);
    s32 b = func_00123660(self, 0);
    s32 show = 0;
    if ((count != 0) && ((a != 0) || (b != 0))) {
        show = 1;
    }
    f32 y0 = 0.0f;
    f32 y = 0.0f;
    if (show != 0) {
        y = 4.0f;
        if (n1 != 0) {
            func_00122A50(self, 1, y);
            func_00121C00(self, 100, y, 50.0f);
            y += 50.0f;
        }
        y0 = y;
        if (n0 != 0) {
            func_00122A50(self, 0, y);
            func_00121C00(self, 100, y, 50.0f);
            y += 50.0f;
        }
    }
    f32 base = y + 10.0f;
    func_00121C00(self, 0x36B, base, 875.0f / self->f2E8);
    func_00121340(self, base);
    if (show != 0) {
        func_00121688(self, y0, n0, n1);
        if (n0 != 0) {
            func_00121590(self, 0, y0);
        }
        if (n1 != 0) {
            func_00121590(self, 1, y0);
        }
    }
    func_00122830(self, base);
    if (n1 != 0) {
        func_001226C8(self, 1, base);
    }
    if (n0 != 0) {
        func_001226C8(self, 0, base);
    }
    func_00121FF0(self, n0, n1, base);
    PDISTD__global_font_manager->f74 = old74;
    {
        Font *f = PDISTD__global_font_manager;
        f->f1C = (f32)oldW;
        f->f20 = (f32)oldH;
    }
    RaceEntryCar__update(1);
    func_004A5400();
    RaceEntryCar__update(0);
    func_004A5400();
    func_004A5688();
    pglPushAttrib(0x887);
    f32 x = rc->x;
    f32 yy = rc->y;
    f32 x2 = x + self->f2EC;
    f32 y2 = yy + self->f2F0;
    f32 u0 = (f32)rt.f10;
    f32 v0 = (f32)rt.f14;
    f32 u1 = (f32)(rt.f10 + rt.f18);
    f32 v1 = (f32)(rt.f14 + rt.f1C);
    func_004AB040(5);
    func_004AB040(4);
    func_004AB040(9);
    func_004AB040(6);
    func_004AB040(3);
    func_004AB040(0xF);
    rt.finish();
    func_004A4550(3, 0);
    func_004A4550(2, 0);
    func_004AB2E8(1.0f, 1.0f, 1.0f);
    func_004A6668(x, yy, x2, y2, u0, v0, u1, v1);
    func_004A1638(6);
    func_004A2808(100, 0x1.555554p-2f);
    func_004A6668(x, yy, x2, y2, u0, v0 + 1.0f, u1, v1 + 1.0f);
    func_004A2808(100, 0.25f);
    func_004A6668(x, yy + 1.0f, x2, y2, u0, v0, u1, v1 - 1.0f);
    func_004AB040(6);
    func_004A5688();
}
/* native correction of src/startup/unit_00101400/func_00120CF0.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_00121C00 */
