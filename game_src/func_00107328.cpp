/* Native copy of 00107328 (matched), the menu models' x squeeze after the frustum: 0.75 in the 16:9 mode, (4/3) / aspect
 * here (gt4_wide_squeeze). (docs/WIDESCREEN.md) */
extern "C" float gt4_wide_squeeze(void);
#include "types.h"
#define NULL 0
void *memcpy(void *, const void *, unsigned int);

extern "C" {
void func_004A7890(f32, f32, f32);
struct func_00107328_arg0 {
    char pad0[0x14];
    f32 unk14;
    char pad18[0x8];
    s32 unk20;
};

void func_00107328(struct func_00107328_arg0 *arg0) {
    f32 a = gt4_wide_squeeze();
    if (arg0->unk20 == 0) a = 1.0f;
    func_004A7890(a, arg0->unk14, 1.0f);
}

}
