/* compiler: ee-gcc2.96-nsa-nogcse */
/* Native code for 004AA6B8 (unmatched; from the decomp's draft, corrected against the original): looks up
 * the scratchpad entry whose key equals `key` in the doubly linked list (head 0xEBA, tail 0xEBB, 20-byte
 * entries at 0xC30), unlinks it, clears its state byte and passes its index to func_004AAD78.
 * The draft put the state clear after the return (dead code), so the entry's state byte (0xC38 + i*20) was
 * never zeroed; the original stores it in the delay slot of the call (sb $zero, 0xC38($v0) at 0x4AA764). */
#include "spr.h"
struct E { int a, b; unsigned char state, pad; signed char prev, next; int c, d; };
struct S {
    unsigned char pad[0xc30]; E entries[32]; char pad2[10]; signed char head, tail;
    int offset(int i) { return i * 20 + 0xc30; }
    E *entry(int i) { return (E *)((char *)this + offset(i)); }
    int key(int i) { char *k = (char *)this + 0x10; return *(int *)(k + offset(i)); }
};
extern "C" void func_004AAD78(S *, int);
extern "C" void func_004AA6B8(int key) {
    S *s = (S *)spr_base();
    if (key >= 0) {
        for (int i = s->head; i >= 0; i = s->entry(i)->next) {
            if (s->key(i) == key) {
                E *e = s->entry(i);
                int prev = e->prev;
                int next = e->next;
                if (prev >= 0) s->entries[prev].next = next;
                else s->head = next;
                if (next >= 0) s->entries[next].prev = prev;
                else s->tail = prev;
                s->entries[i].state = 0;
                func_004AAD78(s, i);
                return;
            }
        }
    }
}
