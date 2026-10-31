/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 00578828 (from its source): changes a thread's priority, with the interrupt-handler variant
   (iChangeThreadPriority) when Status.IE is clear (mfc0 $12 after a sync.p); here gt4_status_ie() (gt4_prelude.h),
   which reads it clear in the runtime's frame callbacks, completions and alarms, as on the EE. */
struct Thread {
    char pad[0x30];
    int id;
};

extern char D_006CC660[]; /* "EE xChangeThreadPriority failed." */

extern "C" void func_00574C30(const char *fmt, ...);
extern "C" int func_00578618(int arg) throw();
extern "C" int func_005785F0(int arg0) throw() __attribute__((const));
extern "C" int func_005ADB30(int thread, int prio);
extern "C" int func_005ADB40(int thread, int prio);

extern "C" void func_00578828(Thread *t, int prio) {
    int id;
    int eePrio;
    int (*change)(int, int);

    id = func_00578618(t->id);
    eePrio = func_005785F0(prio);
    if (!gt4_status_ie())
        change = func_005ADB40;
    else
        change = func_005ADB30;
    if (change(id, eePrio) == -1)
        func_00574C30(D_006CC660);
}
