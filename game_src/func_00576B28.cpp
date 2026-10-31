/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 00576B28 (from its source): invalidates the data cache over [p, p + size), with the interrupt-
   handler variant when Status.IE is clear (mfc0 $12 after a sync.p); here gt4_status_ie() (gt4_prelude.h), which
   reads it clear in the runtime's frame callbacks, completions and alarms, as on the EE. */
extern "C" void func_005AE6D8(void *start, void *end);
extern "C" void func_005AE758(void *start, void *end);

extern "C" void func_00576B28(char *p, int size) {
    void (*sync)(void *, void *);

    sync = func_005AE6D8;
    if (!gt4_status_ie())
        sync = func_005AE758;
    sync(p, p + size - 1);
}
