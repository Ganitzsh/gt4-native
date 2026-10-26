/* Native stand-in for 004AA570 (no matched source yet; closest form differs in scheduling only):
 * reserves a VRAM region of `size` words (frame buffers, render targets) from the page pool and
 * returns its word address. The provisional draft took the size as unsigned short, which the
 * original does not (daddu, no mask): a 640 x 448 frame buffer (0x46000 words) became 12 pages, so
 * texture sets were placed inside the frame buffers. */
#include "spr.h"
struct E { unsigned char state; signed char flag; signed char prev, next; int unk4; int value; int handle; int key; };
struct S { char pad[0xC38]; E entries[32]; char padEB8[2]; signed char head, tail; char padEBC[2]; signed char index; };
extern "C" int func_004AAA20(S *, int);
extern "C" void func_004AAD78(S *, int);

extern "C" int func_004AA570(int size, int arg1) {
    S *s = (S *)spr_base();
    if (s->index >= 0) {
        func_004AAD78(s, s->index);
        s->index = -1;
    }
    int i = func_004AAA20(s, size);
    if (i >= 0) {
        int old = s->head;
        if (old >= 0) s->entries[old].prev = i;
        else s->tail = i;
        s->entries[i].prev = -1;
        s->entries[i].next = old;
        s->head = i;
        s->entries[i].state = 2;
    }
    s->entries[i].key = arg1;
    if (i >= 0) return s->entries[i].value;
    return -1;
}
