/* compiler: ee-gcc2.96-no-strict-aliasing */
#include "spr.h"
// One-time graphics start-up: clears the scratchpad work area, creates its semaphore,
// resets the GS (keeping its revision byte), installs the interrupt handlers, uploads the
// VU microprograms and starts the performance counters. Natively there is no GS: the original's
// writes to its CSR (a reset or a flush, then FINISH acknowledged) and IMR (FINISH unmasked) are
// left out, and the revision byte it kept from CSR is 0 (what the run time's CSR always held).
// The library's VBlank handler (004AB430) is a callback of the runtime's frame clock instead
// (gt4_frame_tick_add, first); its GS (004AB668) and VIF1 (004AB548) interrupt handlers are not
// installed: there is no interrupt controller natively.

struct SemaParam {
    int currentCount;
    int maxCount;
    int initCount;
    int numWaitThreads;
    unsigned int attr;
    unsigned int option;
};

struct State {
    char pad0[0x10];
    int frame;
    char pad14[0x84];
    unsigned char revision;
    char pad99[7];
    int handlerA0;
    char padA4[0x10];
    int handlerB4;
    int handlerB8;
};

struct Spr {
    char pad0[0x50];
    State state;
    char pad10C[4];
    int sema;
    int owner;
    int count;
};

extern unsigned char D_00624DC0;
extern unsigned char D_00624DD0[];
extern unsigned char D_006D3280[];

extern "C" {
void func_0049FEE8() throw();
void func_0049FF38() throw();
void func_004A02A0(Spr *) throw();
void func_004A0890(int, int, int);
int func_004A0BB0(int, int);
int func_004A0B78(int, int, int, int);
int func_004A0D98();
void func_004AA350(unsigned char *, int);
void func_0057FD08(short, short, short, short);
void *func_005A48D8(void *, int, unsigned int);
int func_005ADCA0(SemaParam *);
void func_004AB430();
void func_004AB548();
void func_004AB668();
void func_004A12B8(int, int, int, unsigned char *);
}

extern "C" void func_004A03C0(unsigned char reset, int x, int y, int z)
{
    Spr *spr = (Spr *)spr_base();
    State *state = &spr->state;

    if (D_00624DC0 != 1) {
        SemaParam sema;

        func_005A48D8(spr, 0, 0x1840);
        sema.initCount = 1;
        sema.maxCount = 1;
        spr->sema = func_005ADCA0(&sema);
        spr->owner = -1;
        spr->count = 0;
        func_004A0890(x, y, z);
        if (reset) {
            func_0049FEE8();
            func_0057FD08(0, x, y, z);
        }
        state->revision = 0;
        *(long *)((char *)spr + 0xD0) = -0x201;
        func_004A0BB0(640, 448);
        func_004A0B78(0, 0, 640, 448);
        func_0049FF38();
        func_004A0D98();
        func_004AA350(D_00624DD0, 0x3FF0);
        state->frame = 0;
        state->handlerA0 = gt4_frame_tick_add((void (*)(unsigned int))func_004AB430, 0, 1);
        state->handlerB4 = 0;
        state->handlerB8 = 0;
        func_004A02A0(spr);
        func_004A12B8(0, 0, 0xA80, D_006D3280);
        /* VU0 vi14 = 0x100 (ctc2.i) and the performance counters (mtc0 $25, mtpc): nothing to drive
           natively; VU0 micro mode is not emulated (yet). */
        func_004A12B8(1, 0, 0x3FF0, D_00624DD0);
        D_00624DC0 = 1;
    }
}
