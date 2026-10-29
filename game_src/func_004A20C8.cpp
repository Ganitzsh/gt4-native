/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 004A20C8 (matched with VU0 asm): waits for VIF1, VU1 and the GIF to go idle, resetting each
 * one that stays busy too long. VU1's busy bit is VPU_STAT (VU0 control register vi29) bit 8, its reset FBRST
 * (vi28) bit 9 (RST1): both are __gt4_vu0.vi[] here; nothing sets the busy bit natively, so that wait ends at
 * once. */
#include "gt4_vec.h"
typedef unsigned int u32;
typedef int s32;

#define VIF1_STAT (*(volatile u32 *)0x10003C00)
#define VIF1_FBRST (*(volatile u32 *)0x10003C10)
#define GIF_CTRL (*(volatile u32 *)0x10003000)
#define GIF_MODE (*(volatile u32 *)0x10003010)
#define GIF_STAT (*(volatile u32 *)0x10003020)

extern "C" s32 func_004A0F68(s32 ch);

extern "C" void func_004A20C8(void) {
    s32 timeout = 0x1000000;

    func_004A0F68(1);
    func_004A0F68(2);
    while (VIF1_STAT & 0x1F000003) {
        if (--timeout < 0)
            VIF1_FBRST = 1;
    }
    while (*(volatile u32 *)&__gt4_vu0.vi[29] & 0x100) {   /* VPU_STAT.VBS1: VU1 busy */
        if (--timeout < 0)
            __gt4_vu0.vi[28] |= 0x200;                      /* FBRST.RST1: reset VU1 */
    }
    while (GIF_STAT & 0xC00) {
        if (--timeout < 0) {
            GIF_CTRL = 1;
            GIF_MODE = 4;
        }
    }
}
