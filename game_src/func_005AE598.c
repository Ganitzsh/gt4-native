/* compiler: ee-gcc2.9-991111 */
/* Native version of 005AE598 (cache.o, SyncDCache-like; from its source): writes back the data cache over
   [start, end] (_sceSDC, a no-op natively) with interrupts disabled (DIntr/EIntr) when they were enabled, which
   gt4_interrupts_enabled() tells instead of Status.EIE (mfc0 $12). */
void func_005B72A8(void);
void func_005B72F8(void);
void func_005AE4F0(unsigned int, unsigned int);

void func_005AE598(unsigned int start, unsigned int end) {
    int enabled = gt4_interrupts_enabled();
    if (enabled)
        func_005B72A8();
    func_005AE4F0(start & ~63U, end & ~63U);
    if (enabled)
        func_005B72F8();
}
