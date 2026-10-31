/* compiler: ee-gcc2.96-nogcse */
/* Native version of 005552B8 (matched): a stream thread's loop, run once a frame, woken by its frame callback
 * 00555290 (on its state `c`). The original installed 00555290 as a VBlank interrupt handler (AddIntcHandler2 2,
 * first, `c` as its argument); natively it is a frame callback of the runtime's clock. */
struct Item { int kind; char pad2[0x114 - 4]; };
struct Node { int active; int pad; char payload[1]; };
struct Owner {
    Item items[1];
};
struct Core {
    char pad0[0x4DC];
    int queued;
    char pad4E0[0x4F8 - 0x4E0];
    int thread;
    int running_pad;
    int running;
};
extern "C" {
void func_00555228(void *);
void func_00555268(void *);
void func_00555488(void *);
void func_005555C0(void *, int);
void func_00555678(void *, int);
void func_00555760(void *);
Node *func_005561B8(void *);
int func_00556298(void *, void *);
void func_005562C8(void *, void *);
int func_00560778();
void func_00560A88(int *);
int func_00561300(int, int *);
int func_005782E8(int, int);
void func_005783A0(int);
void func_00555290(unsigned int);
}
extern "C" void func_005552B8(Core *c) {
    int status;
    int out[16];
    int i;
    Item *items = (Item *)((char *)c + 0xC);
    c->thread = func_005782E8(0, 0xFF);
    int saved = gt4_frame_tick_add(func_00555290, (unsigned int)c, 1);
    func_00560778();
    c->running = 1;
    do {
        func_00555488(c);
        func_00560A88(&status);
        if (status == 1 || c->running_pad == 0) {
            func_00555760(c);
        }
        i = 0;
        {
            do {
                int kind = (((Item *)((char *)items + 0x100)) + i)->kind;
                switch (kind) {
                case 1:
                    func_005555C0(c, i);
                    break;
                case 2:
                    func_00555678(c, i);
                    break;
                }
                i++;
            } while (i < 2);
        }
        i = 0;
        int *dst = out;
        for (;;) {
            Node *node = 0;
            func_00555228(c);
            if (c->queued > 0) node = func_005561B8(c);
            func_00555268(c);
            if (node == 0) break;
            if (node->active != 0) {
                i++;
                *dst = func_00556298(c, node->payload);
                dst++;
            } else {
                func_005562C8(c, node->payload);
            }
        }
        if (i > 0) func_00561300(i < 17 ? i : 16, out);
    } while (c->running != 0);
    gt4_frame_tick_remove(saved);
    func_005783A0(c->thread);
}
