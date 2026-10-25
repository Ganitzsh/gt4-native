/* native stand-in, not matched: the network's per-frame event dispatch to the script object
   self->obj (+0x1BC). Unless a dispatch is already running (D_00618E14), it calls the script method
   named by: the pending status message (D_00618E18[func_004F23D8()]), received text (the chunks of
   func_004F2308 joined into one string), a buddy request (func_004F48E8: id, name; the method's
   bool result is answered with func_004F4A40) and a buddy answer (func_004F4B00: status, id,
   name). Each method gets the current context first. Written from the opus draft
   (build/swarm/o1/001fadb0.cpp, structurally identical to the original, 6 instructions off once
   aligned) and checked against the assembly. */
typedef int s32;
typedef short s16;
typedef unsigned int u32;

struct StringRep { u32 len; u32 res; u32 ref; u32 selfish; };
struct String { char *dat; char pad[0xC]; };
struct S00659988 { const char *name; };

struct Info3 {          /* func_004F48E8 copies 36 bytes */
    s32 id;
    char name[0x2C];
};

struct Info4 {          /* func_004F4B00 copies 40 bytes */
    s32 status;
    s32 id;
    char name[0x28];
};

struct Self {
    char pad0[0x1BC];
    void *obj;
};

struct VObj {
    char pad0[4];
    char *vtbl;
};

struct VEntryCall {
    s16 delta;
    s16 index;
    void (*fn)(void *, s32 *, s32, s32 *);
};

struct VEntryInt {
    s16 delta;
    s16 index;
    s32 (*fn)(void *);
};

extern StringRep D_00659FA8;
extern char D_00645570[];
extern char D_006959D8[];
extern s32 D_00618E14;
extern const char *D_00618E18[];
extern char D_00696E40[];
extern char D_00696E50[];
extern char D_00696E68[];
extern char D_00696E88[];
extern char D_00696EA8[];

extern "C" char *func_005C2560(StringRep *r, int unused0);
extern "C" String *func_005C2630(String *str, u32 pos, u32 n1, const char *s, u32 n2);
extern "C" s32 func_0057F260(const char *s);
extern "C" S00659988 *func_005C11A8(int unused0);
extern "C" void func_00326798(void *p, s32 size, s32 align, const char *name);
extern "C" int func_004F23D8(int net);
extern "C" int func_004F2308(void *net, char *buf, int n);
extern "C" s32 func_004F48D8(void *net);
extern "C" void *func_004F48E8(void *out, s32 net);
extern "C" void func_004F4A40(void *net, int a, int b);
extern "C" s32 func_004F4AF0(void *net);
extern "C" void *func_004F4B00(void *out, s32 net);
extern "C" s32 func_004F45F8(void *net, s32 id);     /* game_src/func_004F45F8.c: returns s32 */
extern "C" s32 func_001F1368(int self);
extern void mUpdateContext__Sync(int);
extern "C" void func_002FE278(void *h, void *val);
extern "C" void func_002FC870(void *h, int how);
extern "C" void func_002FA120(s32 h);
extern "C" void func_002F9B38(void *h, int how);
extern "C" int HSymID__GetID(int str);
extern "C" void func_003069F8(void *obj, s32 *h, s32 sym);
extern "C" void func_0030BB18(void *v);
extern "C" void func_00309378(void *v, s32 how, int unused0);
extern "C" void func_002117A0(void *ctx);
extern "C" s32 func_00213158(s32 ctx);
extern "C" s32 func_00251BD8(s32 a, s32 b);
extern "C" void func_00211408(void *ctx, int how);
extern "C" int func_0022AC98(int v, int pval);
extern "C" void func_0022ACC8(void *v, int how);
extern "C" void func_00314B20(void *h, void *str);
extern "C" void func_00312318(void *h, int how);
extern "C" void func_003285A8(void *p);
extern "C" void func_003285F8(void *p);

static inline char *grab(StringRep *r) {
    if (r->selfish)
        return func_005C2560(r, 0);
    ++r->ref;
    return (char *)(r + 1);
}

/* string(const char *) */
static inline void construct(String *str, const char *s) {
    str->dat = grab(&D_00659FA8);
    func_005C2630(str, 0, (u32)-1, s, func_0057F260(s));
}

/* ~string() */
static inline void release(String *s) {
    StringRep *q = (StringRep *)(s->dat - 0x10);
    if (--q->ref == 0)
        func_00326798(q, q->res + 0x10, 4, func_005C11A8(0)->name);
}

/* value = handle (refcounted) */
static inline void set_arg(s32 *dst, s32 *src) {
    if (dst != src) {
        s32 newVal = *src;
        s32 oldVal;
        if (newVal != 0)
            func_003285A8((void *)newVal);
        oldVal = *dst;
        if (oldVal != 0)
            func_003285F8((void *)oldVal);
        *dst = newVal;
    }
}

/* looks the method `name` up on the script object: *h = method (0 if none); ret[0] = its id */
static inline void lookup(Self *self, s32 *h, s32 *ret, const char *name) {
    String s;
    func_002FA120((s32)h);
    construct(&s, name);
    *ret = HSymID__GetID((int)&s);
    func_003069F8(self->obj, h, (s32)ret);
    release(&s);
}

/* args[0] = the current context */
static inline void arg_context(s32 *arg0, s32 *tmp) {
    s32 v;
    func_002117A0(tmp);
    v = func_00251BD8(func_00213158(*tmp), 0);
    func_00211408(tmp, 2);
    func_0022AC98((int)tmp, (int)&v);
    set_arg(arg0, tmp);
    func_0022ACC8(tmp, 2);
}

static inline void arg_int(s32 *arg, s32 *tmp, s32 x) {
    func_002FE278(tmp, (void *)x);
    set_arg(arg, tmp);
    func_002FC870(tmp, 2);
}

static inline void arg_str(s32 *arg, s32 *tmp, String *s) {
    func_00314B20(tmp, s);
    set_arg(arg, tmp);
    func_00312318(tmp, 2);
}

/* calls method h with n arguments, the result in ret */
static inline void vcall(s32 *h, s32 *ret, s32 n, s32 *args) {
    VObj *o = (VObj *)*h;
    VEntryCall *e = (VEntryCall *)(o->vtbl + 0xA8);
    e->fn((char *)o + e->delta, ret, n, args);
}

static inline void destroy_args(s32 *args, s32 n) {
    while (n > 0) {
        --n;
        func_00309378(&args[n], 2, 0);
    }
}

extern "C" void func_001FADB0(Self *self) {
    if (!self->obj)
        return;
    if (D_00618E14)
        return;

    /* the pending status message */
    {
        const char *msg = D_00618E18[func_004F23D8((int)D_00645570)];
        if (msg) {
            s32 h[4], ret[4], args[4], tmp[4];
            lookup(self, h, ret, msg);
            if (h[0]) {
                func_0030BB18(ret);
                func_0030BB18(&args[0]);
                arg_context(&args[0], tmp);
                D_00618E14 = 1;
                vcall(h, ret, 1, args);
                D_00618E14 = 0;
                destroy_args(args, 1);
                func_00309378(ret, 2, 0);
            }
            func_002F9B38(h, 2);
        }
    }

    /* received text */
    if (*(s32 *)(D_00645570 + 0x190) > 0) {
        String str;
        char buf[0x110];
        s32 h[4], ret[4], args[4], tmp[4];
        int r;
        str.dat = grab(&D_00659FA8);
        do {
            r = func_004F2308(D_00645570, buf, 0x100);
            buf[0x100] = 0;
            func_005C2630(&str, ((u32 *)str.dat)[-4], 0, buf, func_0057F260(buf));
        } while (!r);
        lookup(self, h, ret, D_00696E40);
        if (h[0]) {
            func_0030BB18(ret);
            func_0030BB18(&args[0]);
            func_0030BB18(&args[1]);
            arg_context(&args[0], tmp);
            arg_str(&args[1], tmp, &str);
            D_00618E14 = 1;
            vcall(h, ret, 2, args);
            D_00618E14 = 0;
            destroy_args(args, 2);
            func_00309378(ret, 2, 0);
        }
        func_002F9B38(h, 2);
        release(&str);
    }

    /* buddy request: the method's bool answer is sent back */
    if (func_004F48D8(D_00645570)) {
        Info3 inf;
        s32 h[4], ret[4], args[4], tmp[4];
        func_004F48E8(&inf, (s32)D_00645570);
        lookup(self, h, ret, D_00696E50);
        if (h[0]) {
            String name;
            func_0030BB18(ret);
            func_0030BB18(&args[0]);
            func_0030BB18(&args[1]);
            func_0030BB18(&args[2]);
            arg_context(&args[0], tmp);
            arg_int(&args[1], tmp, inf.id);
            construct(&name, inf.name);
            arg_str(&args[2], tmp, &name);
            release(&name);
            D_00618E14 = 1;
            vcall(h, ret, 3, args);
            D_00618E14 = 0;
            {
                VObj *o = (VObj *)ret[0];
                VEntryInt *e = (VEntryInt *)(o->vtbl + 0x58);
                s32 ok = e->fn((char *)o + e->delta);
                func_004F4A40(D_00645570, ok == 1, inf.id);
            }
            destroy_args(args, 3);
            func_00309378(ret, 2, 0);
        }
        func_002F9B38(h, 2);
    }

    /* buddy answer */
    if (func_004F4AF0(D_00645570)) {
        Info4 inf;
        s32 h[4], ret[4], args[4], tmp[4];
        const char *method;
        func_004F4B00(&inf, (s32)D_00645570);
        func_002FA120((s32)h);
        if (inf.status == -0x3D9) {
            method = D_00696E68;
        } else if (inf.status == 2) {
            while (!func_004F45F8(D_00645570, inf.id))
                mUpdateContext__Sync(1);
            if (func_001F1368((int)self))
                method = D_00696E88;
            else
                method = D_00696EA8;
        } else {
            method = D_00696EA8;
        }
        {
            String s;
            construct(&s, method);
            *ret = HSymID__GetID((int)&s);
            func_003069F8(self->obj, h, (s32)ret);
            release(&s);
        }
        if (h[0]) {
            String name;
            func_0030BB18(ret);
            func_0030BB18(&args[0]);
            func_0030BB18(&args[1]);
            func_0030BB18(&args[2]);
            arg_context(&args[0], tmp);
            arg_int(&args[1], tmp, inf.id);
            construct(&name, inf.name);
            arg_str(&args[2], tmp, &name);
            release(&name);
            D_00618E14 = 1;
            vcall(h, ret, 3, args);
            D_00618E14 = 0;
            destroy_args(args, 3);
            func_00309378(ret, 2, 0);
        }
        func_002F9B38(h, 2);
    }
}
