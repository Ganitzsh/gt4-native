/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0054A1D8 (matched): 0054A230's thread's frame callback: while D_0064C3F0 is set, a frame
 * counted (at most two ahead, D_0064C3F4) wakes the thread (semaphore D_0064C3F8). The original was its VBlank
 * interrupt handler (ending with ExitHandler); natively the runtime's frame clock calls it. */
extern volatile int D_0064C3F0;
extern int D_0064C3F4;
extern int D_0064C3F8;
extern "C" void func_00578480(int sema);
extern "C" void func_0054A1D8(unsigned int) {
    if (D_0064C3F0) {
        int n = D_0064C3F4;
        if (n < 2) {
            D_0064C3F4 = n + 1;
            func_00578480(D_0064C3F8);
        }
    }
}
