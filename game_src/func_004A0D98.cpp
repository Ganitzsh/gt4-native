/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 004A0D98 (from its source): the graphics library's DMA set-up at boot (004A03C0). It resets its
   transfer channels 0-2 (004A0D00: their flags and semaphores). The original also wrote the DMA controller (D_STAT,
   D_PCR, D_CTRL) and added and enabled the DMA end handler 004AB6D8 on each channel: there is no DMA controller
   natively (the frame is sent by runtime/frame.cpp), so the handler ids it kept are 0. The scratchpad base is the
   plain address (spr.h loads it with a MIPS `li`). */
struct State { char pad[0xa4]; int handles[3]; };
extern "C" void func_004A0D00(int,int) throw();
extern "C" int func_004A0D98() {
    State *s = (State *)((char *)0x70002000 + 0x50);
    func_004A0D00(0,0x40);
    func_004A0D00(1,0xc0);
    func_004A0D00(2,0x80);
    s->handles[0] = 0;
    s->handles[1] = 0;
    s->handles[2] = 0;
    return 0;
}
