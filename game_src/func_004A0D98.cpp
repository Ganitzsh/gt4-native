/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 004A0D98 (from its source): the graphics library's DMA set-up at boot (004A03C0). It still resets
   channels 0-2 (004A0D00: their register words and semaphores), adds the DMA end handler 004AB6D8 to each and enables
   them, but no longer writes the DMA controller: D_STAT &= 0x70007 (acknowledge and mask), D_PCR &= 0x7FF8FFF8 and
   D_CTRL = 1 (DMA enable), with the `sync` after them. Natively nothing reads those three registers: runtime/gs.cpp
   moves the graphics channels' data itself and the runtime's kernel raises the DMAC handlers it enabled. The scratchpad
   base is the plain address (spr.h loads it with a MIPS `li`). */
struct State { char pad[0xa4]; int handles[3]; };
extern "C" void func_004A0D00(int,int) throw();
extern "C" int func_005AD9B0(int, void *,int);
extern "C" int func_005AE8B8(void *);
extern char D_004AB6D8[];
extern "C" int func_004A0D98() {
    State *s = (State *)((char *)0x70002000 + 0x50);
    func_004A0D00(0,0x40);
    func_004A0D00(1,0xc0);
    func_004A0D00(2,0x80);
    s->handles[0] = func_005AD9B0(0,D_004AB6D8,0);
    s->handles[1] = func_005AD9B0(1,D_004AB6D8,0);
    s->handles[2] = func_005AD9B0(2,D_004AB6D8,0);
    func_005AE8B8((void *)0);
    func_005AE8B8((void *)1);
    return func_005AE8B8((void *)2);
}
