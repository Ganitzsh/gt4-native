/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native copy of 004A0D00 (matched): resets one of the graphics library's transfer channels (0-2) at boot
 * (004A0D98): its busy and done flags and a new semaphore. The original also wrote the DMA channel's registers
 * (CHCR = `chcr`, the tag, address and stack registers zeroed); there is no DMA controller natively (the frame is
 * sent by runtime/frame.cpp), so `chcr` goes unused. */
#include "spr.h"
struct SemaParam { int currentCount, maxCount, initCount, numWaitThreads; unsigned int attr, option; };
struct Chan { unsigned char busy, done; char pad[6]; int sema; };
struct Sub { char pad[0x1C]; Chan chan[3]; };
struct Spr { char pad[0x50]; Sub sub; };
extern "C" int func_005ADCA0(SemaParam *);
extern "C" void func_004A0D00(int ch, unsigned int chcr) {
    Spr *s = (Spr *)spr_base();
    Sub *sub = &s->sub;
    SemaParam param;
    (void)chcr;
    sub->chan[ch].busy = 0;
    sub->chan[ch].done = 0;
    param.initCount = 1;
    param.maxCount = 1;
    sub->chan[ch].sema = func_005ADCA0(&param);
}
