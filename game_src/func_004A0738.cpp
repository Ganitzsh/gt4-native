/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native copy of 004A0738 (matched): the graphics library's shutdown, without its writes to hardware: the GS
 * interrupt mask to the GS's IMR (the library's copy at +0xD0 is kept) and its last step (mtc0 to the performance
 * counter control and a sync.p). Its VBlank handler is the frame callback 004A03C0 added (removed here); the GS,
 * VIF1 and DMA interrupt handlers it removed were never installed natively. */
#include "spr.h"
typedef unsigned char u8;
typedef unsigned long u64;

struct Sub {
    char pad0[0xA0];
    int fA0;
    char padA4[0xB4 - 0xA4];
    int fB4;
    int fB8;
};

struct Spr {
    char pad0[0xD0];
    u64 fD0;
    char padD8[0x110 - 0xD8];
    int f110;
};

extern u8 D_00624DC0;
extern "C" void func_004AA498(Spr *);
extern "C" int func_004A0E90();
extern "C" void func_005ADCB0(int);

extern "C" void func_004A0738(void) {
    u8 *flag = &D_00624DC0;
    Spr *p = (Spr *)spr_base();
    Sub *q = (Sub *)((char *)p + 0x50);
    if (*flag != 0) {
        func_004AA498(p);
        func_004A0E90();
        func_005ADCB0(p->f110);
        gt4_frame_tick_remove(q->fA0);   /* the frame callback 004A03C0 added (no interrupt handlers) */
        u64 m = 0xFFFFFFFF;
        p->fD0 = m;
        /* (the EE's performance counter control register, PCCR, cleared: none natively) */
        *flag = 0;
    }
}
