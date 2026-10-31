/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 00551728 (matched): 00551758's thread's frame callback, which wakes it once a frame (its
 * semaphore D_0064C718). The original was its VBlank interrupt handler (ending with ExitHandler); natively the
 * runtime's frame clock calls it (gt4_frame_tick_add). */
extern int D_0064C718;
extern "C" void func_00578480(int);
extern "C" void func_00551728(unsigned int) {
    func_00578480(D_0064C718);
}
