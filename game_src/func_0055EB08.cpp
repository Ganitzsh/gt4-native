/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0055EB08 (matched): 0055EB50's thread's frame callback: a frame counted (at most two ahead of
 * the thread, D_00654A3C) wakes it (semaphore D_00654A40). The original was its VBlank interrupt handler (ending
 * with ExitHandler); natively the runtime's frame clock calls it. */
extern int D_00654A3C;
extern int D_00654A40;
extern "C" void func_00578480(int);
extern "C" void func_0055EB08(unsigned int) {
    int n = D_00654A3C;
    if (n < 2) {
        D_00654A3C = n + 1;
        func_00578480(D_00654A40);
    }
}
