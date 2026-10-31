/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 00578400 (from its source): polls the semaphore of arg, with the interrupt-handler variant
   (iPollSema) when Status.IE is clear (mfc0 $12 after a sync.p); here gt4_status_ie() (gt4_prelude.h), which reads
   it clear in the runtime's frame callbacks, completions and alarms, as on the EE. */
extern "C" int func_00578290(int arg) throw();
extern "C" int func_005ADCF0(int sema);
extern "C" int func_005ADD00(int sema);

extern "C" int func_00578400(int arg) {
    int sema;
    int ret;
    int (*poll)(int);

    sema = func_00578290(arg);
    if (sema == -1)
        return sema;
    if (!gt4_status_ie())
        poll = func_005ADD00;
    else
        poll = func_005ADCF0;
    ret = poll(sema);
    if (ret == sema)
        return arg;
    return ret;
}
