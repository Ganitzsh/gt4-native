/* Native copy of the provisional draft for 005293D0: the first now() callback gets info and out, which the EE forwards to it in $a1 and $a2 (passthrough_lint) */
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned long long u64;
typedef u64 Id __attribute__((aligned(4)));
struct Info { Id id; int x8; int xc; u16 port; u16 x12; int x14; int x18; int x1c; };
struct Obj { char p0[0x50]; int x50; int x54; int x58; };
struct Conn {
    int state; int x4; Id id; int x10; char q14[0x18]; char q2c[0x18]; char t44[8]; char t4c[0x18];
    int x64; int x68; u16 x6c; u16 x6e; int x70; u16 x74; char p76[10];
    int x80; int x84; int x88; int x8c; int x90; u16 x94; char p96[0x402]; int x498;
};
struct Api { char p0[0x6c]; void (*now)(u64 *); int x70; int (*same)(u64, Id); };
struct Reg {
    char p0[0x18]; void (*bind)(int, int, void *); void (*name)(int, int, void *);
    char p20[0xc]; void (*add)(int, int, void *); char p30[0x24]; void (*init)(void *);
};
extern struct Api *D_0064B4B0;
extern struct Reg *D_0064B710;
extern int func_00529228(struct Obj *, u64 *, int *);
extern int func_005292C0(struct Obj *, u64 *, int *);
extern u32 func_00528940(u32);
extern int func_005288E8(void);
extern struct Conn *func_0052AA18(struct Obj *);
extern int func_0052D448(void *, int);
extern int func_0052BDD8(struct Obj *, struct Conn *);
extern void func_0053A860(void *);

int func_005293D0(struct Obj *o, struct Info *info, struct Conn **out) {
    u64 key[2];
    char buf0[0x40];
    char buf1[0x40];
    int idx;
    struct Conn *c;
    if (!out) return 2;
    *out = 0;
    if (!o) return 0xd2f2;
    if (!info->x14) return 2;
    if (!info->x18) return 2;
    ((void (*)(u64 *, struct Info *, struct Conn **))D_0064B4B0->now)(key, info, out);
    idx = 0;
    func_00529228(o, key, &idx);
    if (info->x8 == idx) {
        if (D_0064B4B0->same(key[0], info->id)) { fail: return 0xd305; }
    }
    D_0064B4B0->now(key);
    idx = 0;
    func_005292C0(o, key, &idx);
    if (info->x8 == idx) {
        if (D_0064B4B0->same(key[0], info->id)) goto fail;
    }
    c = func_0052AA18(o);
    if (!c) return 0xd2f7;
    *out = c;
    c->x94 = 0;
    if (func_0052D448(c->q14, o->x54) == 0 && func_0052D448(c->q2c, o->x58) == 0) {
        c->state = 10;
        c->x74 = func_00528940(0xffff);
        c->x6e = func_00528940(0x3ff);
        c->x10 = info->x8;
        c->x498 = info->x1c;
        c->x8c = info->x18;
        c->x6c = info->port;
        c->id = info->id;
        c->x90 = info->x14;
        func_0053A860(c->t44);
        func_0053A860(c->t4c);
        c->x68 = 1000;
        c->x64 = 0;
        c->x80 = info->xc;
        if (func_005288E8() == 1) {
            D_0064B710->init(buf1);
            D_0064B710->add(-1, c->x84, buf1);
            D_0064B710->name(-1, o->x50, buf0);
            D_0064B710->bind(-1, c->x84, buf0);
        }
        if (func_0052BDD8(o, c) == 0) return 0;
    }
    c->state = 7;
    return 0xd2f7;
}
