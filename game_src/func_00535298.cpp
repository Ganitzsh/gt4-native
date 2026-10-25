#include "types.h"
struct Clock { char pad[0x5c]; u32 ticks; };
struct List { char pad0[0x104]; int selected; char pad1[0x144-0x108]; int state; char pad2[8]; int count; };
struct Item { char pad[0x20]; int active; };
struct Context { char pad0[0x9c]; void (*before)(); void (*after)(); int (*check)(); char pad1[0x108-0xa8]; int enabled; char pad2[8]; u32 last; List *list; };
extern Clock *D_0064B47C;
extern Context *D_0064B4B4;
extern u32 D_0064B4E0;
extern "C" void func_0053A7B8(u32 *);
extern "C" int func_00535010(int) throw();
extern "C" Item *func_00535C88(List *, u32);
extern "C" void func_00534B90(int) throw();
extern "C" int func_00535E30();
extern "C" int func_00536158(int);
extern "C" int func_00537E68();
extern "C" int func_005364A0();
extern "C" int func_00535298() {
    u32 first, now;
    int r;
    func_0053A7B8(&first);
    if ((r = func_00535010(1)) == 0) {
        func_0053A7B8(&now);
        u32 *prev = &D_0064B4E0;
        Clock *clock = D_0064B47C;
        u32 time = now;
        u32 elapsed = time - *prev;
        u32 ticks = clock->ticks + elapsed;
        *prev = time;
        clock->ticks = ticks;
        Context *ctx = D_0064B4B4;
        if (ctx != 0) {
            List *list = ctx->list;
            if (list != 0) {
                if (ctx->last + 3000U < ticks && list->state == 1 && list->selected < list->count) {
                    for (int i = 0; i < list->count; i++) {
                        Item *p = func_00535C88(list, i);
                        if (p != 0 && i != list->selected && p->active != 0) func_00534B90(i);
                    }
                    if (D_0064B4B4 != 0) D_0064B4B4->last = D_0064B47C->ticks;
                }
                if (D_0064B4B4 != 0 && list->state == 1 && list->selected < list->count && D_0064B4B4->enabled != 0) {
                    D_0064B4B4->before();
                    D_0064B4B4->after();
                }
            }
        }
        if ((r = func_00535E30()) == 0 && (r = func_00536158(1)) == 0 && (D_0064B4B4 == 0 || (r = D_0064B4B4->check()) == 0) && (r = func_00537E68()) == 0) r = func_005364A0();
    }
    return r;
}
/* native correction of src/network/unit_00494578/func_00535298.cpp: returns the last call's result: func_00535010's when nonzero, else the first nonzero of the final checks or func_005364A0's */
