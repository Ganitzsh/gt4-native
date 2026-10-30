/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native copy of 003B80E8 (matched): the factor it passes is 0.911 at 4:3 and 0.911 / 0.75 in the 16:9 mode; here
 * 0.911 / ((4/3) / aspect) (gt4_wide_squeeze). (docs/WIDESCREEN.md) */
extern "C" float gt4_wide_squeeze(void);
typedef int s32;
extern s32 D_006184F0;
extern "C" void func_00105280(void *, s32);
extern "C" int func_00105378(void *, s32);
extern "C" void func_00105440(void *, s32);
extern "C" void func_00105450(void *, s32);
extern "C" void func_001054A0(void *, s32);
extern "C" int func_00105528(void *);
extern "C" void func_001056A0(void *);
extern "C" s32 func_00105818(void *);
extern "C" void func_00497AF0(void *, float, s32, s32, s32, s32, s32, s32, s32, s32, s32, s32);
extern "C" void RaceEntryCar__update(s32);
extern "C" void pglPushAttrib(s32);
extern "C" void func_004A7550(void *);
extern "C" void func_004AB040(s32);

static inline s32 first(s32 *p) { return *p; }

extern "C" void func_003B80E8(char *arg0, s32 arg1) {
    s32 sp20[16];
    char *s4 = arg0 + 0x8C;
    char *s3 = arg0 + 0xC8;
    pglPushAttrib(0x8C7);
    func_00105440(s4, 0x40);
    float f = 0x1.d26e96p-1f / gt4_wide_squeeze();
    func_00105450(s4, 8);
    *(s32 *)(s4 + 4) = 0;
    func_00105528(s4);
    func_00105440(s3, 0x40);
    func_00105450(s3, 0x40);
    *(s32 *)(s3 + 4) = 0;
    func_00105528(s3);
    func_004AB040(8);
    s32 *ps = sp20;
    func_00105378(ps, arg1);
    func_001054A0(ps, 0);
    func_001056A0(ps);
    if (D_006184F0 == 0) f = 0x1.d26e96p-1f;
    s32 s0 = first(ps);
    s32 s1 = sp20[2];
    func_00497AF0(arg0, f, s0, s1, func_00105818(ps), *(s32 *)s4, 8, *(s32 *)s3, 0x40, 0x40, *(s32 *)(arg0 + 0x7C), *(s32 *)(arg0 + 0x84));
    *(s32 *)(arg0 + 0xA28) = 0;
    *(s32 *)(arg0 + 0xA34) = 0;
    *(s32 *)(arg0 + 0x1848) = 0;
    RaceEntryCar__update(1);
    func_004A7550(arg0 + 0x21B0);
    RaceEntryCar__update(0);
    *(float *)(arg0 + 0x21AC) = *(float *)(arg0 + 0x21C4);
    func_00105280(ps, 2);
}
