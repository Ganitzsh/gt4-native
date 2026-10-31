/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 004EFAA8 (from its source): signals a device's event, from an interrupt handler (the wait
   queue wake-up 00576A48) or from a thread (under the device's lock). The original tells them apart by Status.IE
   (mfc0 $12 after a sync.p); here gt4_status_ie() (gt4_prelude.h), which reads it clear in the runtime's frame
   callbacks, completions and alarms, as on the EE. */
struct Device {
    char pad[0x38];
    char event[0x30];
    char lock[0x30];
    void *waiters;
    char pad9C[0x214 - 0x9C];
    int running;
};

extern int D_006454DC;
extern "C" int func_004EFA68(void) throw();
extern "C" void func_00574EE8(void *);
extern "C" void func_00576788(void *);
extern "C" void func_005767C0(void *);
extern "C" void func_00576A48(void *);

extern "C" void func_004EFAA8(Device *dev) {
    if (!gt4_status_ie()) {
        if (dev->running && D_006454DC == 0)
            return func_00576A48(dev->event);
    } else if (func_004EFA68()) {
        func_00576788(dev->lock);
        if (dev->running)
            func_00574EE8(dev->event);
        func_005767C0(dev->lock);
        D_006454DC = 0;
    }
}
