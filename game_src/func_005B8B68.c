/* compiler: ee-gcc2.9-991111 */
/* Native version of 005B8B68 iSetTimerHandler (from its source): sets or clears an alarm timer's handler and
   reschedules the timers; the handler keeps the caller's $gp, now from gt4_get_gp() instead of `sw $28`. */
struct Timer {
    int m0;
    int m4;
    int id;
    int flags;
    long long base;
    long long total;
    unsigned long long schedule;
    void *handler;
    void *gp;
    void *common;
    int pad;
};

struct TimerCtl {
    char pad[0x1C];
    volatile int current;
};

extern struct TimerCtl D_006592F0;
extern void func_005B8120(struct Timer *);
extern void func_005B8098(struct Timer *);
extern long long func_005B8400(void);
extern void func_005B7F08(long long);

/* iSetTimerHandler */
int func_005B8B68(int id, unsigned long long schedule, void *handler, void *common)
{
    struct Timer *t = (struct Timer *)(((unsigned int)id >> 10) << 6);

    if (id < 0 || (id & 0x3FF) != t->id)
        return 0x80008002;
    if (D_006592F0.current == id)
        return 0x80000010;
    if (t->flags & 2)
        func_005B8120(t);
    t->handler = handler;
    if (handler == 0) {
        t->flags &= 0xFFFFFFFDU;
    } else {
        t->schedule = schedule;
        t->flags |= 2;
        /* the handler runs with the caller's module gp */
        t->gp = gt4_get_gp();
        t->common = common;
        if (t->flags & 1)
            func_005B8098(t);
    }
    func_005B7F08(func_005B8400());
    return 0;
}
