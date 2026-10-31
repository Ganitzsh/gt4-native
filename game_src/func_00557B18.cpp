/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 00557B18 (matched): a thread that runs its hook once a frame (under the lock D_00872280) until
 * `stop` is set, woken by its frame callback 00557AF0 through its semaphore. The original installed 00557AF0 as a
 * VBlank interrupt handler (AddIntcHandler2 2, first, the semaphore as its argument); natively it is a frame
 * callback of the runtime's clock. */
struct Thread {
    char pad[0x288];
    int handle;
    int stop;
    void (*hook)(int);
    int hookArg;
};
extern "C" int func_005782E8(int, int);
extern "C" void func_00557AF0(unsigned int);
extern "C" void func_00578500(int);
extern "C" void func_005783A0(int);
extern "C" void func_00576788(int);
extern "C" void func_005767C0(int);
extern char D_00872280[];
extern "C" void func_00557B18(Thread *t) {
    t->handle = func_005782E8(0, 0xFF);
    int tick = gt4_frame_tick_add(func_00557AF0, (unsigned int)t->handle, 1);
    if (t->stop == 0) {
        do {
            func_00578500(t->handle);
            func_00576788((int)D_00872280);
            void (*hook)(int) = t->hook;
            int arg = t->hookArg;
            if (hook != 0) hook(arg);
            func_005767C0((int)D_00872280);
        } while (t->stop == 0);
    }
    t->stop = 0;
    gt4_frame_tick_remove(tick);
    func_005783A0(t->handle);
}
