/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native copy of 004A0738 (matched): the graphics library's shutdown, without its writes to hardware: the GS
 * interrupt mask to the GS's IMR (the library's copy at +0xD0 is kept) and its last step (mtc0 to the performance
 * counter control and a sync.p). */
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
extern "C" int func_005AE780(int);
extern "C" int func_005AD9A0(int, int);

extern "C" void func_004A0738(void) {
    u8 *flag = &D_00624DC0;
    Spr *p = (Spr *)spr_base();
    Sub *q = (Sub *)((char *)p + 0x50);
    if (*flag != 0) {
        func_004AA498(p);
        func_004A0E90();
        func_005ADCB0(p->f110);
        func_005AE780(0);
        func_005AE780(5);
        func_005AE780(2);
        func_005AD9A0(0, q->fB8);
        func_005AD9A0(5, q->fB4);
        func_005AD9A0(2, q->fA0);
        u64 m = 0xFFFFFFFF;
        p->fD0 = m;
        /* (the EE's performance counter control register, PCCR, cleared: none natively) */
        *flag = 0;
    }
}
