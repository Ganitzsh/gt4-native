/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 00577F80 (from its source): waits 2000 microseconds (DelayThread) in a thread, or calls
   005763B0 inside an interrupt handler, told by Status.IE being clear (mfc0 $12 after a sync.p); here
   gt4_status_ie() (gt4_prelude.h), which reads it clear in the runtime's frame callbacks, completions and alarms,
   as on the EE. */
extern "C" void func_005763B0(void);
extern "C" void func_005AED18(int usec);

extern "C" void func_00577F80(void) {
    if (!gt4_status_ie())
        func_005763B0();
    else
        func_005AED18(2000);
}
