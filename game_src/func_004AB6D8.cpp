/* compiler: ee-gcc2.96-no-strict-aliasing */
/* native stand-in, not matched: written from the assembly (the provisional draft, at distance 30/150,
   lost the GIF tag messages). The graphics library's DMA interrupt handler for channels 0-2.
   In chain mode, a tag with its IRQ bit is a message: on channel 1 a GS -> EE download step, on
   channel 2 (PATH3 texture streaming, in step with VIF1's interrupt codes, 004AB548) the tag's user
   bits 18 (count up x18, VIF1 resumes at 0), 16 (count down x17: the GIF waits while it is negative,
   else goes on at once) and 17 (count up x16, VIF1 resumes at 0); without bit 16 the GIF goes on.
   Otherwise the transfer is over: the channel's in-flight flag is cleared and its waiters woken. */
#include "spr.h"

/* state (spr_base() + 0x50): +0x10 + 12 * channel the channel's waiter list, +0xC done, +0xD busy;
   +0x15 download steps, +0x16..0x18 the PATH3/VIF1 counters, +0x44 the download list */
struct Waiter { struct Waiter *next; int thread; };
struct Dma { volatile unsigned int chcr; char pad[0x1c]; volatile unsigned int qwc; };

extern "C" unsigned int *D_00629DC8[];
extern "C" void func_004AB930(char *s);
extern "C" void func_004AB9C0(char *s);
extern "C" int func_005AEB68(int thread);

#define VIF1_FBRST (*(volatile unsigned int *)0x10003c10)

extern "C" int func_004AB6D8(int ch)
{
    volatile char *s = (volatile char *)spr_base() + 0x50;
    unsigned int chcr = *D_00629DC8[ch];
    struct Waiter *w;

    if (((chcr >> 2) & 3) == 1) {  /* chain mode */
        unsigned int id = (chcr >> 28) & 7;
        if (id == 7 || (id == 6 && ((chcr >> 4) & 3) != 0))
            goto over;
        if (!(chcr & 0x80000000u)) {  /* no tag IRQ: the end of the chain */
            s[0x10 + ch * 12 + 0xC] = 1;
            goto over;
        }
        if (ch == 1) {
            s[0x15] = s[0x15] + 1;
            if ((signed char)s[0x15] == 2) func_004AB930((char *)s);
        } else if (ch == 2) {
            if (chcr & 0x40000) {
                s[0x18] = s[0x18] + 1;
                if ((signed char)s[0x18] == 0) VIF1_FBRST = 8;
            } else {
                int go = 1;
                if (chcr & 0x10000) {
                    s[0x17] = s[0x17] - 1;
                    if ((signed char)s[0x17] < 0) go = 0;
                }
                if (go) {
                    struct Dma *d = (struct Dma *)0x1000a000;
                    unsigned int v = d->chcr;
                    d->qwc = 0;
                    d->chcr = v | 0x100;
                    __asm__ volatile("sync");
                }
                if (chcr & 0x20000) {
                    s[0x16] = s[0x16] + 1;
                    if ((signed char)s[0x16] == 0) VIF1_FBRST = 8;
                }
            }
        }
        __asm__ volatile("sync");
        __asm__ volatile("ei");
        return 1;
    }
    /* normal mode */
    if ((signed char)s[0x15] == 2) {
        func_004AB930((char *)s);
        __asm__ volatile("sync");
        __asm__ volatile("ei");
        return 1;
    }
    if (*(volatile int *)(s + 0x44) != 0 && s[0x15] == 0) {
        func_004AB9C0((char *)s);
        __asm__ volatile("sync");
        __asm__ volatile("ei");
        return 1;
    }
over:
    s[0x10 + ch * 12 + 0xD] = 0;
    for (w = *(struct Waiter **)(s + 0x20 + ch * 12); w; w = w->next)
        func_005AEB68(w->thread);
    __asm__ volatile("sync");
    __asm__ volatile("ei");
    return 1;
}
