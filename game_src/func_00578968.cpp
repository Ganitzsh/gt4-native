typedef int s32;
typedef unsigned int u32;

struct P8 { s32 a; s32 b; P8() {} P8(s32 x, s32 y) { a = x; b = y; } };

struct ThreadParam {
    s32 status;
    void *entry;
    void *stack;
    s32 stackSize;
    s32 gp;
    s32 prio;
    s32 cur;
    s32 pad[5];
};

struct Blk { s32 base; s32 size; };
struct Out { s32 base; s32 size; s32 a; s32 b; };

struct Node {
    char pad0[0x30];
    s32 id;
    char pad34[4];
    P8 m38;
    s32 m40;
    s32 m44;
    P8 m48;
    Node(s32 v) __asm__("func_00574FC8");
};

inline void *operator new(u32, void *p) { return p; }

extern "C" void func_00579CB8(Out *out, Blk *in, s32 align);
extern "C" s32 func_005785F0(s32 a);
extern "C" s32 func_00578CF0(s32 a, s32 b);
extern "C" void func_00577F80(void);
extern "C" s32 func_005ADAA0(ThreadParam *p);
extern "C" void *func_005A48D8(void *, s32, u32);
extern char func_005786F0[];
extern char D_006DDDF0[];
extern s32 D_00874950[];

extern "C" Node *func_00578968(P8 *name, Blk *arg, s32 prio) {
    s32 owned = 0;
    s32 size = arg->size;
    if (size == 0) size = 0x2000;
    s32 base = arg->base;
    if (base == 0) {
        owned = 1;
        base = func_00578CF0(0x10, size);
    }
    Blk in;
    in.base = base;
    in.size = size;
    Out out;
    func_00579CB8(&out, &in, 0x10);
    s32 obase = out.base;
    u32 osz = out.size;
    s32 off = 0;
    u32 hdr = 0x50;
    if (osz > hdr) off = osz - hdr;
    ThreadParam tp;
    func_005A48D8(&tp, 0, 0x30);
    tp.entry = func_005786F0;
    tp.stack = (void *)obase;
    tp.stackSize = off;
    tp.gp = (s32)D_006DDDF0;
    tp.prio = func_005785F0(prio);
    Node *n = (Node *)(obase + off);
    if (n) new (n) Node(0);
    s32 id;
    while ((id = func_005ADAA0(&tp)) == -1) {
        func_00577F80();
    }
    u32 cnt = (D_00874950[id] + 1) & 0xFFFF;
    D_00874950[id] = cnt;
    n->id = id | (cnt << 8);
    n->m38 = *name;
    n->m44 = owned;
    n->m48 = P8(base, off);
    return n;
}
