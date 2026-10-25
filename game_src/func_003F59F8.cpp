/* compiler: ee-gcc2.96-no-strict-aliasing */
typedef int s32;
typedef short s16;
typedef unsigned char u8;
typedef float f32;

struct VEntry {
    s16 delta;
    s16 index;
    void *(*fn)(char *, s32);
};

struct Settings { char pad[0xE0]; s32 e0; s32 e4; };
struct Ev { char pad[0x64]; char *vt64; };
struct Ev2 { char pad[0x84]; char *p84; };

extern "C" s32 func_0034C190(void *arg0, s32 arg1);
extern "C" s32 func_00345228(void *p);
extern "C" s32 func_00345520(char *p);
extern "C" void func_0035DF80(void *p, s32 a, s32 b, s32 c);
extern "C" void func_00363528(char *p);
extern "C" void func_003F37B8(void *p) throw();
extern "C" s32 func_003F5820(void *, void *, s32, f32) throw();
extern "C" void func_003F58B0(void *self, void *arg, s32 lap, f32 t) throw();

extern "C" void DynamicsConductorFreeRun__ProcessLap0ControlLine(char *arg0, s32 arg1, s32 arg2, s32 arg3, f32 t) {
    char *s3 = (char *)func_0034C190(arg0, arg1);
    char *s1 = *(char **)arg0;
    char *s5 = s3 + 0x104;
    char *s2;
    if (arg1 == 0) {
        s2 = *(char **)(s1 + 0x84);
        if (*(u8 *)(s5 + 0x467) == 1) {
            func_00345228(s3);
            func_003F37B8(s3);
            func_0035DF80(s1, 4, 0, 0);
        }
        if (*(s32 *)(s1 + 0xE0) == 0 && *(s32 *)(arg0 + 0xF8A0) == 2) {
            VEntry *e = (VEntry *)(*(char **)(s2 + 0x64) + 0x240);
            func_003F58B0(e->fn(s2 + e->delta, 1), (void *)func_0034C190(arg0, 1), 0, t);
        }
        if (*(u8 *)(arg0 + 0xCBDC) == 0) {
            VEntry *e = (VEntry *)(*(char **)(s2 + 0x64) + 0x240);
            func_003F5820(e->fn(s2 + e->delta, 0x100), s3, arg3, t);
        }
    }
    if (*(u8 *)(s5 + 0x441) != 0 || *(s32 *)(*(char **)arg0 + 0xE4) != 0) {
        func_00345520(s3);
    }
    func_00363528(s3);
    *(s32 *)(s5 + 0x614) = arg2;
}
/* native correction of src/race/DynamicsConductorFreeRun/DynamicsConductorFreeRun__ProcessLap0ControlLine.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_003F5820 */
