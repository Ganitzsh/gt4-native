/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 00576AD8 (from its source): writes back the data cache over [p, p + size), with the interrupt-
   handler variant when Status.IE is clear (mfc0 $12 after a sync.p); here gt4_status_ie() (gt4_prelude.h), which
   reads it clear in the runtime's frame callbacks, completions and alarms, as on the EE. */
extern "C" void func_005AE598(void *start, void *end);
extern "C" void func_005AE618(void *start, void *end);

extern "C" void func_00576AD8(char *p, int size) {
    void (*sync)(void *, void *);

    sync = func_005AE598;
    if (!gt4_status_ie())
        sync = func_005AE618;
    sync(p, p + size - 1);
}
