/* compiler: ee-gcc2.96-exceptions-as2004 */
/* Native copy of the provisional draft for 0051CD28: the first time() callback gets a, which the EE forwards to it in $a1 (passthrough_lint) */
typedef unsigned int u32;
typedef unsigned long long u64;
typedef u64 U64 __attribute__((aligned(4)));
struct Pair { int a, b; };
enum Kind { KIND_0, KIND_1 };
struct Arg { enum Kind type; U64 id; int size; int x10, x14, x18, x1c, x20, x24, x28; void *data; u32 len; int x34, x38, x3c, x40, x44, x48; };
struct Node { int x0, x4, x8, xc; struct Pair id; int size; void *handle; char p20[0x10]; int x30; char p34[8]; int x3c, x40, x44; char p48[0x44]; int x8c, x90; };
struct Obj { int id; u32 type; int state; struct Arg arg; char p58[0x20]; struct Node *nodes[2]; struct Node *active; int x84, x88, x8c, x90, x94, x98; };
struct Api { char p0[0x14]; int (*open)(void *, u64, int, void *); char p18[0x54]; void (*time)(u64 *); };
struct Api2 { char p0[0x14]; int (*config)(int, int, int); char p18[0x44]; int (*select)(int); };
extern struct Api *D_0064B4B0;
extern struct Api2 *D_0064B710;
extern u32 D_0064AE10;
extern int func_0051D920(int *, u32, int, int);
extern struct Obj *func_0051DD50(u32);
extern int func_0051D448(struct Obj *);
extern int func_00538BF8(void *, u64);
extern int func_005380B8(void *);
extern int func_0053A860(void *);
extern void *func_005A4724(void *, const void *, u32);
extern void func_005A48D8(void *, int, u32);

int func_0051CD28(int *out, struct Arg *a)
{
    int opts[3];
    u64 now[2];
    int id = 0;
    int r = 2;
    struct Obj *o;
    struct Node **pp;
    struct Node *p;
    unsigned i;
    int v38, v40, v3c, v44, v48, v34;

    if (!a->size) return r;
    if (!a->x38) return r;
    if (a->type >= 2) return r;
    ((void (*)(u64 *, struct Arg *))D_0064B4B0->time)(now, a);
    *out = D_0064AE10 + 1;
    r = func_0051D920(&id, a->type, a->x24, a->x28);
    if (r) return r;
    *out = id;
    if (D_0064B710) {
        D_0064B710->select(id & 0x7fffffff);
        D_0064B710->config(id & 0x7fffffff, 0, a->x18);
    }
    o = func_0051DD50(id);
    o->state = 0;
    func_005A4724(&o->arg, a, 0x4c);
    if (a->data && a->len) {
        r = func_00538BF8(&o->arg.data, (u64)a->len);
        if (r) return r;
        func_005A4724(o->arg.data, a->data, a->len);
        o->arg.len = a->len;
    } else {
        o->arg.data = 0;
        o->arg.len = 0;
    }
    i = 0;
    pp = o->nodes;
    D_0064B4B0->time((u64 *)((char *)o + 0x64));
    v38 = a->x38; v40 = a->x40; v3c = a->x3c; v44 = a->x44; v48 = a->x48; v34 = a->x34;
    o->x88 = v38; o->x8c = v40; o->x90 = v3c; o->x94 = v44; o->x98 = v48; o->x84 = v34;
    do {
        p = *pp;
        pp++;
        if (p) {
            p->x8c = 0;
            p->x90 = 0;
            func_0053A860((char *)p + 0x20);
            func_0053A860((char *)p + 0x28);
            p->x30 = a->x1c;
            p->x3c = 0;
            func_0053A860((char *)p + 0x34);
            p->x40 = a->x20;
            p->x44 = 0;
            func_005380B8((char *)p + 0x54);
            func_005380B8((char *)p + 0x6c);
        }
        i++;
    } while (i < 2);
    o->state = 2;
    if (o->type == 1) {
        p = o->nodes[1];
        if (!p) return 3;
        func_005A48D8(opts, 0, 12);
        opts[1] = 0x248;
        opts[0] = 1;
        r = D_0064B4B0->open(&p->handle, now[0], a->x14, opts);
        if (r) return r;
        p->x4 = 0;
    }
    if (o->type < 2) {
        p = o->nodes[0];
        if (!p) return 3;
        p->id = *(struct Pair *)&a->id;
        p->size = a->size;
        func_005A48D8(opts, 0, 12);
        opts[2] = 1;
        opts[0] = 0;
        r = D_0064B4B0->open(&p->handle, a->id, a->size, opts);
        if (r) return r;
        p->x4 = 1;
        o->active = p;
    }
    if (!o->active) return 3;
    if (o->id != o->active->x8) return 0xcb20;
    return func_0051D448(o);
}
