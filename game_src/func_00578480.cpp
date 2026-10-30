/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 00578480 (from its source): signals the semaphore of arg, once with the interrupt-handler
   variant (iSignalSema) when Status.IE is clear (mfc0 $12 after a sync.p), else until it succeeds with a 2 ms wait
   between tries; here gt4_status_ie() (gt4_prelude.h), which reads 1 as the translated mfc0 always did, so the
   thread path runs. */
extern "C" int func_00578290(int arg) throw();
extern "C" void func_00577F80(void) throw();
extern "C" int func_005ADCC0(int sema);
extern "C" int func_005ADCD0(int sema);

extern "C" void func_00578480(int arg) {
    int sema;

    sema = func_00578290(arg);
    if (sema == -1)
        return;
    if (!gt4_status_ie()) {
        func_005ADCD0(sema);
    } else {
        while (func_005ADCC0(sema) == -1)
            func_00577F80();
    }
}
