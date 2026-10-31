/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0054A230 (matched): a thread that runs 0054A168 once a frame while D_0064C3EC is set, woken by
 * its frame callback 0054A1D8 (semaphore D_0064C3F8, frames counted in D_0064C3F4, taken back with interrupts
 * disabled). The original installed 0054A1D8 as a VBlank interrupt handler (AddIntcHandler 2, first); natively
 * it is a frame callback of the runtime's clock. */
extern "C" void func_0054A168() throw();
extern "C" int func_005782E8(int,int);
extern "C" void func_005783A0(int);
extern "C" void func_00578500(int);
extern "C" int func_005B72A8();
extern "C" void func_0054A1D8(unsigned int);
extern int D_0064C3F4;
extern char D_0064C3F8[];
struct Flag { int v; };
extern volatile Flag D_0064C3EC;
static inline int active(volatile Flag *const p) { return p->v; }
extern "C" void func_0054A230() {
    int *count = &D_0064C3F4;
    *count = 0;
    { int *q = (int *)D_0064C3F8; *q = func_005782E8(0,255); }
    int tick = gt4_frame_tick_add(func_0054A1D8, 0, 1);
    while (active(&D_0064C3EC)) {
        func_00578500(*(int *)D_0064C3F8);
        int enabled = func_005B72A8();
        --*count;
        if (enabled) gt4_interrupts_enable();
        func_0054A168();
    }
    int *q = (int *)D_0064C3F8;
    func_00578500(*q);
    int enabled = func_005B72A8();
    --D_0064C3F4;
    if (enabled) gt4_interrupts_enable();
    gt4_frame_tick_remove(tick);
    func_005783A0(*q);
}
