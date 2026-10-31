/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 00555290 (matched): 005552B8's thread's frame callback (005554B0 on its state, given as
 * `core`). The original was its VBlank interrupt handler (AddIntcHandler2's argument in its second parameter,
 * ending with ExitHandler); natively the runtime's frame clock calls it with that argument. */
extern "C" void func_005554B0(void *);
extern "C" void func_00555290(unsigned int core) {
    func_005554B0((void *)core);
}
