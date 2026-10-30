/* compiler: ee-gcc2.9-991111 */
/* Native version of 005AE6D8 (cache.o, InvalidDCache-like; from its source): invalidates the data cache over
   [start, end] (_sceIDC, a no-op natively) with interrupts disabled (DIntr/EIntr) when they were enabled, which
   gt4_interrupts_enabled() tells instead of Status.EIE (mfc0 $12). */
void func_005B72A8(void);
void func_005B72F8(void);
void func_005AE630(unsigned int, unsigned int);

void func_005AE6D8(unsigned int start, unsigned int end) {
    int enabled = gt4_interrupts_enabled();
    if (enabled)
        func_005B72A8();
    func_005AE630(start & ~63U, end & ~63U);
    if (enabled)
        func_005B72F8();
}
