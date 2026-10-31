/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 00551758 (matched): a thread that runs 00551580 once a frame while D_0064C714 is set, woken
 * by its frame callback 00551728 through the semaphore D_0064C718. The original installed 00551728 as a VBlank
 * interrupt handler (AddIntcHandler 2, first); natively it is a frame callback of the runtime's clock. */
typedef int s32;
extern "C" s32 func_005782E8(s32, s32);
extern "C" void func_005783A0(s32);
extern "C" void func_00578500(s32);
extern "C" void func_00551580(void) throw();
extern "C" void func_00551728(unsigned int);
extern char D_0064C718[];
struct Flag { int v; };
extern volatile Flag D_0064C714;
static inline int active(volatile Flag *const p) { return p->v; }
extern "C" void func_00551758(void) {
    {
        s32 *q = (s32 *)D_0064C718;
        *q = func_005782E8(0, 255);
    }
    int tick = gt4_frame_tick_add(func_00551728, 0, 1);
    while (active(&D_0064C714) != 0) {
        func_00578500(*(s32 *)D_0064C718);
        func_00551580();
    }
    s32 *q = (s32 *)D_0064C718;
    func_00578500(*q);
    gt4_frame_tick_remove(tick);
    func_005783A0(*q);
}
