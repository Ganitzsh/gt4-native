/* native stand-in, not matched: a script-engine native of the online unit (sibling of MNetwork__adduser,
   func_001DEB68): opens the network context, logs in with the context's name, then, when the session is
   ready, sends the two string arguments (func_001F2248) and waits through the connection steps; returns a
   bool handle in *ret (true only when every step succeeded). Written from the opus draft
   (build/swarm/o1/001def80.cpp, 13 instructions off once aligned) and checked against the assembly. */
typedef int s32;
typedef short s16;
typedef unsigned int u32;

struct StringRep { u32 len; u32 res; u32 ref; u32 selfish; };
struct Str { char *p; };
struct S00659988 { const char *name; };

struct VEntry18 { s16 delta; s16 index; void (*fn)(Str *, void *); };
struct Obj { char pad0[4]; char *vtbl; };

extern StringRep D_00659FA8;
extern char D_008297E0[];

extern "C" char *func_005C2560(StringRep *r, int unused0);
extern "C" Str *func_005C2630(Str *str, u32 pos, u32 n1, const char *s, u32 n2);
extern "C" s32 func_0057F260(const char *s);
extern "C" S00659988 *func_005C11A8(int unused0);
extern "C" void func_00326798(void *p, s32 size, s32 align, const char *name);
extern "C" void func_001DC650(void *h, void *ctx);
extern "C" void func_001DC5F8(void *h, int how);
extern "C" u32 func_001F1578(int obj);
extern "C" const char *func_001DC9C8(char *obj);
extern "C" long func_001F1600(void *obj, char **str, int a, int b);
extern "C" s32 func_001F16A8(int obj);
extern "C" void *func_001F20C8(void *obj);
extern "C" void *func_001F21D8(void *obj);
extern "C" int func_001F2248(s32 obj, Str *a, Str *b);   /* game_src/func_001F2248.cpp: returns int */
extern "C" void *func_001F2748(void *obj, int name);
extern "C" void *func_001F2358(void *obj);
extern "C" void *func_001F2958(void *obj);
extern "C" void *func_001F5BB8(void *obj);
extern "C" int func_001F29A8(void *obj);                 /* game_src/func_001F29A8.c: returns int */
extern "C" void func_002FE278(void *h, void *b);
extern "C" void func_002FC870(void *h, int how);
extern "C" void func_003285A8(void *p);
extern "C" void func_003285F8(void *p);

static inline char *grab(StringRep *r) {
    if (r->selfish)
        return func_005C2560(r, 0);
    ++r->ref;
    return (char *)(r + 1);
}

/* string(const char *) */
static inline void construct(Str *str, const char *s) {
    str->p = grab(&D_00659FA8);
    func_005C2630(str, 0, (u32)-1, s, func_0057F260(s));
}

/* ~string() */
static inline void str_release(Str *s) {
    StringRep *q = (StringRep *)(s->p - 0x10);
    if (--q->ref == 0)
        func_00326798(q, q->res + 0x10, 4, func_005C11A8(0)->name);
}

/* the virtual method at vtable slot 0x18: returns a string */
static inline void vcall_18(Str *ret, Obj *o) {
    VEntry18 *e = (VEntry18 *)(o->vtbl + 0x18);
    e->fn(ret, (char *)o + e->delta);
}

/* *ret = bool handle h made from b */
static inline void set_bool(s32 *ret, s32 *h, s32 b) {
    func_002FE278(h, (void *)b);
    if (ret != h) {
        s32 newVal = *h;
        s32 oldVal;
        if (newVal != 0)
            func_003285A8((void *)newVal);
        oldVal = *ret;
        if (oldVal != 0)
            func_003285F8((void *)oldVal);
        *ret = newVal;
    }
    func_002FC870(h, 2);
}

extern "C" void func_001DEF80(s32 *ret, void *ctx, s32 argc, Obj **args) {
    s32 h0[4];
    union { Str s; s32 h[4]; } u;
    union { Str s; s32 h[4]; } b;
    s32 h3[4];
    s32 ok;

    func_001DC650(h0, ctx);
    if (func_001F1578(h0[0])) {
        set_bool(ret, u.h, 0);
        func_001DC5F8(h0, 2);
        return;
    }
    {
        const char *name = func_001DC9C8((char *)h0[0]);
        s32 obj = h0[0];
        s32 x = *(s32 *)(obj + 0x30);
        long r;
        construct(&u.s, name);
        r = func_001F1600((void *)obj, &u.s.p, x, 1);
        str_release(&u.s);
        if (r == 0) {
            set_bool(ret, u.h, 0);
            func_001DC5F8(h0, 2);
            return;
        }
    }
    if (!func_001F20C8((void *)h0[0]) || !func_001F2748((void *)h0[0], (int)D_008297E0) ||
        !func_001F21D8((void *)h0[0])) {
        func_001F16A8(h0[0]);
        set_bool(ret, u.h, 0);
        func_001DC5F8(h0, 2);
        return;
    }
    vcall_18(&u.s, args[0]);
    vcall_18(&b.s, args[1]);
    ok = 0;
    if (func_001F2248(h0[0], &u.s, &b.s) == 0) {
        func_001F16A8(h0[0]);
    } else if (func_001F16A8(h0[0]) == 0 || func_001F2358((void *)h0[0]) == 0) {
        /* no extra call */
    } else if (!func_001F2958((void *)h0[0]) || !func_001F5BB8((void *)h0[0]) ||
               !func_001F29A8((void *)h0[0])) {
        func_001F16A8(h0[0]);
    } else {
        ok = 1;
    }
    set_bool(ret, h3, ok);
    str_release(&b.s);
    str_release(&u.s);
    func_001DC5F8(h0, 2);
}
