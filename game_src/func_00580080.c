/* compiler: ee-gcc2.9-991111 */
/* Native stand-in (not matched): libcdvd's sceCdInitEeCB(priority, stack, stack_size) at
 * 00580080, which the function inventory (build/functions.csv) does not list: it is glued to the
 * callback thread 0057FFB0 before it, so the image has no trampoline here until the inventory
 * splits 0057FFB0 at 00580080 (it is reached through libcdvd's export table, 0x0068AE68).
 * The first call creates and starts the callback thread (entry 0057FFB0, the given stack, $gp 0,
 * option "SceCdCallbackThread") and returns 1; later calls change the thread's priority and
 * return 0. */
struct ThreadParam {
    int status;
    void *func;
    void *stack;
    int stack_size;
    void *gp_reg;
    int initial_priority;
    int current_priority;
    unsigned int attr;
    unsigned int option;
};

extern volatile int D_00655ED4;          /* callback thread id */
extern struct ThreadParam D_00874D98;
extern char D_006CDFC8[];                /* "SceCdCallbackThread" */

void func_0057FFB0(void);
int func_005ADAA0(struct ThreadParam *); /* CreateThread */
int func_005AEDE0(int thread, void *arg); /* StartThread */
int func_005ADB30(int thread, int priority); /* ChangeThreadPriority */

int func_00580080(int priority, void *stack, int stack_size)
{
    struct ThreadParam *p;
    if (D_00655ED4 != 0) {
        func_005ADB30(D_00655ED4, priority);
        return 0;
    }
    p = &D_00874D98;
    p->stack_size = stack_size;
    p->func = (void *)func_0057FFB0;
    p->stack = stack;
    p->initial_priority = priority;
    p->option = (unsigned int)D_006CDFC8;
    p->gp_reg = 0;
    D_00655ED4 = func_005ADAA0(p);
    func_005AEDE0(D_00655ED4, 0);
    return 1;
}
