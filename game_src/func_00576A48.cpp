/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 00576A48 (from its source): from an interrupt handler, wakes every waiter of q
   (iWakeupThread), or moves them to its pending list while it is deferred. The original acts only when Status.IE
   is clear (mfc0 $12 after a sync.p), i.e. inside the EE kernel's handlers; here gt4_status_ie() (gt4_prelude.h),
   which reads it clear in the runtime's frame callbacks, completions and alarms, as on the EE. */
struct Waiter {
    Waiter *next;
    int thread;
};

struct WaitQueue {
    char pad0[8];
    Waiter *head;
    Waiter *tail;
    Waiter *pendHead;
    Waiter *pendTail;
    char pad18[8];
    int deferred;
};

extern "C" int func_005AEB68(int thread);

extern "C" void func_00576A48(WaitQueue *q) {
    Waiter *w;
    Waiter *last;
    int thread;

    if (!gt4_status_ie()) {
        w = q->head;
        last = q->tail;
        q->head = 0;
        q->tail = 0;
        if (q->deferred == 0) {
            while (w != 0) {
                thread = w->thread;
                w = w->next;
                func_005AEB68(thread);
            }
        } else if (w != 0) {
            if (q->pendTail != 0)
                q->pendTail->next = w;
            else
                q->pendHead = w;
            q->pendTail = last;
        }
    }
}
