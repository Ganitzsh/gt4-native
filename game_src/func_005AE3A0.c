/* compiler: ee-gcc2.9-991111 */
/* Native version of 005AE3A0 sbrk (from its source): moves the heap break by increment with interrupts disabled;
   past the limit the kernel reports, sets errno to ENOMEM and returns -1. The Status reads, di/sync.p and ei become
   gt4_interrupts_enabled(), gt4_interrupts_disable() and gt4_interrupts_enable(); the loop that repeats di until
   Status.EIE reads clear (the EE's di can be lost to an interrupt) is kept as it was translated. */
extern unsigned D_006582A4;
extern unsigned func_005ADC80(void);
extern int *func_005A3130(void);
void *func_005AE3A0(int increment) {
    int enabled;
    unsigned next;
    enabled = gt4_interrupts_enabled();
    if(enabled) {
        do {
            gt4_interrupts_disable();
        } while(gt4_interrupts_enabled());
    }
    next=D_006582A4+increment;
    if(func_005ADC80()<next) {
        *func_005A3130()=12;
        if(enabled) gt4_interrupts_enable();
        return (void *)-1;
    } else {
        void *previous=(void *)D_006582A4;
        D_006582A4=next;
        if(enabled) gt4_interrupts_enable();
        return previous;
    }
}
