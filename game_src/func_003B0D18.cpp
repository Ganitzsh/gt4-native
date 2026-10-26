/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native correction of the provisional draft of 003B0D18 (HumanModel): ModelSet2::searchModelName
 * (004548A8) stores a 32-bit count; the draft gave it a u16 local, and natively the store ran over
 * the frame (a crash at the race start). The count is a u32 here, read as its low 16 bits as the
 * original does (lhu). */
typedef int s32;
typedef unsigned int u32;
typedef short s16;
typedef unsigned short u16;
typedef float f32;

struct Ent { char pad0[4]; u16 a; char pad6[0x3A]; u32 f40; };
struct Kv { char pad0[4]; f32 x; f32 y; };
struct Mdl { char pad0[0x760]; Ent *tab[20]; };

struct HM {
    void *obj;
    char pad4[0x6B0];
    s32 f6B4;
    s32 f6B8;
    u16 f6BC[2];
    u16 f6C0[2];
    s32 f6C4;
    char pad6C8[0x40];
    f32 f708;
    f32 f70C;
    struct { u16 a, b; } f710[20];
    u16 f760;
    u16 f762;
    Ent *f764[20];
};

extern "C" s32 func_0041B3D0(u32);
extern "C" Ent *func_004548A8(void *, s32, u32 *);
extern "C" Ent *func_00454970(void *, s32);
extern "C" Kv *func_00454B08(void *, u32);
extern s32 D_006219C0[];
extern char D_006A23A0[];

static inline Ent **lookup(char *b, int offset) { return (Ent **)(b + offset); }

extern "C" void func_003B0D18(HM *self) {
    u32 cnt;
    Kv *kv;
    Ent *e;
    s32 i;
    s32 id;
    kv = func_00454B08(self->obj, 0x800001);
    if (kv != 0) {
        self->f708 = kv->x;
        self->f70C = kv->y;
    }
    e = func_004548A8(self->obj, (s32)D_006A23A0, &cnt);
    self->f6B8 = 0;
    if (e != 0) {
        u16 n = (u16)cnt;
        u16 a = e->a;
        self->f762 = n;
        self->f760 = a;
        if ((s16)(u16)cnt > 0) self->f6B8 = 1;
    }
    i = 0;
    char *b = (char *)self + 4;
    for (; i < 20; i++) {
        id = func_0041B3D0(i);
        self->f764[i] = func_00454970(self->obj, id);
        if (self->f764[i] != 0 && self->f764[i]->f40 >= 20) self->f764[i] = 0;
        e = func_004548A8(self->obj, id, &cnt);
        if (e != 0) {
            self->f710[i].a = e->a;
            self->f710[i].b = (u16)cnt;
        }
    }
    self->f6B4 = 0;
    i = 0;
    id = -1;
    s32 *q = (s32 *)(b + 0x6c0);
    e = func_004548A8(self->obj, D_006219C0[0], &cnt);
    u16 *pair = self->f6C0;
    if (e != 0) {
        pair[0] = e->a;
        pair[1] = (u16)cnt;
    } else {
        pair[0] = id;
        pair[1] = id;
    }
    s32 j = -1;
    if (i == 0) j = 14;
    if (j >= 0) {
        Ent **tab = lookup(b, j * 4 + 0x760);
        if (*tab != 0) {
            self->f6B4 = 1;
            *q = (*tab)->f40;
            return;
        }
    }
    pair[0] = id;
    *q = 0;
}
