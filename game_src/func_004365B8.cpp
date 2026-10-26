/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native copy of 004365B8 (matched: applies the option block) with the host's display shape
 * (docs/WIDESCREEN.md). With --aspect wider than 1.5 the game's own 16:9 mode is switched on through
 * its setter 00437110 (the option's wide_mode, and the matching monitor preset at +0x24), and the
 * monitor's width becomes height x aspect: the race projection (OptionMonitorMatrix, m0 = 2 d / w)
 * keeps its vertical field of view and widens horizontally (Hor+) for any aspect. The 16:9 picture
 * is anamorphic in the 640 x 448 frame; the host presents it at the aspect. */
typedef int s32;
typedef unsigned char u8;
typedef float f32;
extern s32 D_006184F0;
extern char PDISTD__UNIT_MANAGER[];
extern "C" void func_004A0BE0(s32, s32);
extern "C" s32 func_004390D0(void *);
extern "C" void func_00472528(void *, s32);
extern "C" void func_00439888(void *);
extern "C" int func_00460AF8(s32);
extern "C" void func_00437110(char *, s32);
extern "C" int gt4_display_aspect_milli(void); /* runtime: the --aspect (x 1000), 0 when none */

struct Config {
    char pad0[0xC];
    s32 unkC;
    char pad10[0x8];
    s32 unk18;
    s32 unk1C;
    char pad20[0x1090];
    s32 unk10B0;
};
struct Units {
    s32 unk0, unk4, unk8;
};
struct Monitor { /* the physical monitor at +0x24 */
    char pad0[0xC];
    f32 width;
    f32 height;
};

extern "C" void func_004365B8(s32 *config) {
    const int aspect = gt4_display_aspect_milli();
    if (aspect >= 1500) {
        func_00437110((char *)config, 1);
        Monitor *m = (Monitor *)((char *)config + 0x24);
        m->width = m->height * (f32)aspect / 1000.0f;
    } else if (aspect > 0) {
        func_00437110((char *)config, 0);
    }
    Units *units = (Units *)PDISTD__UNIT_MANAGER;
    Config *c = (Config *)config;
    func_004A0BE0(c->unk18 << 1, c->unk1C << 1);
    D_006184F0 = func_004390D0((char *)config + 0x24);
    func_00472528(units, c->unkC);
    units->unk0 = *(u8 *)((char *)config + 0x10);
    units->unk4 = *(u8 *)((char *)config + 0x11);
    units->unk8 = *(u8 *)((char *)config + 0x12);
    func_00439888((char *)config + 0x1650);
    func_00460AF8(c->unk10B0);
}
