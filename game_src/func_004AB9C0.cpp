/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 004AB9C0 (from its draft): runs the object's callback, then resets the GS, VIF1 and DMA
   channels 1 and 2 through their registers; without the `sync` after each channel write. */
typedef unsigned int u32;
struct Callback { int unk0; int arg0; void (*fn)(int, int); int arg1; };
struct Obj { char pad0[0x44]; Callback *cb; };
struct Dma { u32 control; char pad4[0x1C]; u32 addr; };
extern "C" void func_004AB9C0(Obj *o) {
    Callback *cb = o->cb;
    if (cb->fn) cb->fn(cb->arg0, cb->arg1);
    volatile unsigned long *gs = (volatile unsigned long *)0x12001040;
    volatile u32 *vif = (volatile u32 *)0x10003C00;
    volatile Dma *d = (volatile Dma *)0x10009000;
    u32 control = d->control;
    *gs = 0;
    *vif = 0;
    d->addr = 0;
    d->control = (control & 0xFEF0) | 0x10000105;
    d = (Dma *)0x1000A000;
    control = d->control;
    d->addr = 0;
    d->control = control | 0x100;
}
