/* compiler: ee-gcc2.9-991111 */
/* Native version of 005AE8B8 (from its source): EnableDmac (the syscall stub 005ADA00) on arg with interrupts disabled
   (DIntr/EIntr) when they were enabled, which gt4_interrupts_enabled() tells instead of Status.EIE (mfc0 $12);
   without the `sync.l` after the call. */
int func_005B72A8(void);
int func_005B72F8(void);
int func_005ADA00(void *arg);

int func_005AE8B8(void *arg)
{
    int status;
    int ret;

    status = gt4_interrupts_enabled();
    if (status)
        func_005B72A8();
    ret = func_005ADA00(arg);
    if (status)
        func_005B72F8();
    return ret;
}
