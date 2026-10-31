/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 00557AF0 (matched): 00557B18's thread's frame callback, which wakes it (the semaphore given as
 * `sema`). The original was its VBlank interrupt handler (AddIntcHandler2's argument in its second parameter,
 * ending with ExitHandler); natively the runtime's frame clock calls it with that argument. */
extern "C" void func_00578480(int);
extern "C" void func_00557AF0(unsigned int sema) {
    func_00578480((int)sema);
}
