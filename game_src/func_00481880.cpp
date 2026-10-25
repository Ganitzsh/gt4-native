/* compiler: ee-gcc2.96-no-strict-aliasing */
/* native stand-in, not matched (2722 instructions, never drafted): strobe::ActionInstance::doAction,
   the Flash ActionScript (SWF action record) interpreter of the movie runtime. Runs the action
   bytes [code, code + len) on the value stack of ctx (ctx->164): one-byte actions 0x04..0x68 through
   the jump table at 0x006AE0B0, actions >= 0x80 with a 16-bit length through the one at 0x006AE250
   (GotoFrame, StoreRegister, ConstantPool, With, Push, Jump, DefineFunction, If), four registers and
   an eight-deep `with` scope stack. Returns the value of an ActionReturn in *result (undefined
   otherwise) and returns result (a Val returned in memory). Written from the assembly, handler by
   handler; every value operation is the unit's own matched helper. */
typedef int s32;
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
typedef float f32;

/* the script value: type (1 undefined, 2 null, 3/4 string, 6 number, 7 object, 13 function...) */
struct Val {
    s32 type;
    union { s32 i; f32 f; void *p; } v;
};
struct WithEntry { Val val; u32 end; };          /* a `with` scope, active while pc < end */
struct WithStack { WithEntry e[8]; s32 top; };
struct ValStack { char pad[8]; Val *top; };       /* the context's value stack (one past the top) */
struct StringRep { u32 len, res, ref, selfish; };
struct String { char *p; };                      /* the game's libstdc++ v2 basic_string */
struct Cursor { u8 *p; };

extern StringRep D_00659E20;                     /* the empty string's representation */
extern char *D_00624838[];                       /* the 22 property names (_x, _y, ...) */
extern char D_006ADF38[], D_006ADF40[], D_006ADF48[], D_006ADF58[]; /* PLAY STOP NEXTFRAME PREVFRAME */
extern char D_006ADF68[];                        /* "%s\n" */
extern char D_006ADF70[];                        /* "" */
extern char D_006ADF78[];                        /* "new object %s\n" */
extern char D_006ADF88[];                        /* "... Class %s ..." (constructor not found) */
extern char D_006ADFB8[];                        /* "new method %s\n" */
extern char D_006ADFC8[], D_006AE000[];          /* ActionGetProperty / ActionSetProperty target messages */
extern char D_006AE038[], D_006AE040[];          /* VALUEOF TOSTRING */
extern char D_006AE050[];                        /* "unsupported action : %x\n" */
extern char D_006AE070[];                        /* GOTOANDSTOP */
extern char D_006AE080[];                        /* "unsupported action : %x  length : %d\n" */

/* strings */
extern "C" char *func_005D2B58(StringRep *rep);                  /* strobe__toUpper: unshare */
extern "C" String *func_005D2C20(String *s, u32 pos, u32 n1, const char *src, u32 n2);
extern "C" u32 func_0057F260(const char *s);                     /* strlen */
extern "C" void func_00575DA0(void *p);                          /* free */
extern "C" void func_00480FE8(String *s);                        /* to upper case (a-z) */
extern "C" void func_0057D9C0(const char *fmt, ...);
/* values */
extern "C" s32 func_00476768(Val *dst, const Val *src);          /* copy construct */
extern "C" void *func_00476790(Val *dst, const Val *src);        /* assign */
extern "C" void func_004768C0(Val *v);                           /* destroy */
extern "C" void func_00476698(Val *ret, String *name, char *ctx); /* global lookup */
extern "C" void func_004769E0(Val *v);                           /* = null */
extern "C" void func_00476A08(Val *v);
extern "C" void func_00476A38(Val *v);                           /* = new array */
extern "C" void func_00476AE8(Val *v, const char *s);            /* = string */
extern "C" void func_00476B30(Val *v, s32 b);                    /* = boolean */
extern "C" void func_00476B78(Val *v, f32 f);                    /* = number */
extern "C" void func_00476BC0(Val *v, s32 i);                    /* = number from an integer */
extern "C" void func_00476BE8(Val *v, u8 *code, s32 size, s32 nparams, u8 *params, void *table, void *pool);
extern "C" void func_00476D40(Val *result, Val *ctor, char *self, u32 nargs, void *pool, char *ctx);
extern "C" char *func_00477298(void *f, int unused0, int unused1);
extern "C" char *func_004772A8(Val *v);                          /* strobe::Any::toString */
extern "C" f32 func_00477460(Val *v);                            /* to number */
extern "C" s32 func_004774A8(Val *v);                            /* to boolean */
extern "C" void func_004774F8(Val *ret, Val *obj, String *name); /* get member */
extern "C" int func_00477738(Val *obj, String *name, Val *value, int create); /* set member */
extern "C" void func_00477890(Val *ret, Val *fn, char *self, u32 nargs, void *pool, char *ctx); /* call */
extern "C" void func_00477C40(Val *ret, Val *obj, String *name, char *self, u32 nargs, void *pool, char *ctx); /* call method */
extern "C" void func_00477D98(Val *obj, ValStack *st);           /* enumerate */
extern "C" void func_00477DE0(Val *a, const Val *b);             /* a += b */
extern "C" s32 func_00477F28(const Val *a, const Val *b);        /* a < b */
extern "C" s32 func_00478000(const Val *a, const Val *b);        /* a > b */
extern "C" s32 func_004780F8(const Val *a, const Val *b);        /* string a > b */
extern "C" bool func_00478178(const Val *a, const Val *b);       /* a == b */
extern "C" s32 func_00478250(const Val *a, const Val *b);        /* a === b */
extern "C" void func_00478280(Val *a, Val *b);                   /* a -= b */
extern "C" void func_004782D0(Val *a, Val *b);                   /* a *= b */
extern "C" void func_00478320(Val *a, Val *b);                   /* a /= b */
extern "C" void func_00478378(Val *a, Val *b);                   /* a %= b */
extern "C" void func_00478400(Val *a);                           /* !a */
extern "C" void func_00478480(Val *a, Val *b);                   /* a &= b */
extern "C" void func_004784E0(Val *a, Val *b);                   /* a && b */
extern "C" void func_00478540(Val *a, Val *b);                   /* a |= b */
extern "C" void func_004785A0(Val *a, Val *b);                   /* a ^= b */
extern "C" void func_00478600(Val *a, Val *b);                   /* a <<= b */
extern "C" void func_00478660(Val *a, Val *b);                   /* a >>= b */
extern "C" void func_004786C0(Val *a, Val *b);                   /* a >>>= b */
extern "C" void func_00478748(Val *a);                           /* ++a */
extern "C" void func_00478780(Val *a);                           /* --a */
extern "C" void func_004787B8(Val *a, int base);                 /* to number */
extern "C" const char *func_00478A18(Val *a);                    /* strobe::Any::typeOf */
extern "C" s32 func_004809D8(void *node, String *name, Val *value);
extern "C" void func_00480F20(ValStack *st, const Val *v);       /* push */
extern "C" void func_00480F78(ValStack *st);                     /* pop */
extern "C" void func_00480FA0(ValStack *st, s32 n);              /* pop n */
extern "C" void func_00481190(Val *ret, void *cb, String *name, u32 nargs, ValStack *st);
extern "C" Val *func_00481230(void *table, String *name);
extern "C" void *func_00481280(void *table, String *name);
extern "C" Val *func_00481410(void *map, String *name);          /* map[name] */
extern "C" s32 func_00481698(Cursor *c);                         /* u16 */
extern "C" s32 func_004816C0(Cursor *c);                         /* u32 */
extern "C" void func_00484308(Val *ret, char *self);             /* `this` */
extern "C" void func_00484530(Val *ret, char *self, String *name, void *table, WithStack *with, char *ctx);
extern "C" void func_00484748(char *self, char *path, void *table, WithStack *with, char *ctx, char **start, Val *target);
extern "C" void func_00485368(void *pool, u8 *data);             /* ConstantPool */
extern "C" const char *func_00485498(void *pool, s32 index);
extern "C" void *func_00475D98(char *ctx);
extern "C" f32 func_00475DA8(char *ctx);

static inline void str_init(String *s, const char *src) {
    StringRep *r = &D_00659E20;
    if (r->selfish) {
        s->p = func_005D2B58(r);
    } else {
        r->ref++;
        s->p = (char *)(r + 1);
    }
    func_005D2C20(s, 0, (u32)-1, src, func_0057F260(src));
}
static inline void str_free(String *s) {
    StringRep *r = (StringRep *)(s->p - 0x10);
    if (--r->ref == 0)
        func_00575DA0(r);
}
static inline const char *str_c(String *s) {
    u32 len = ((StringRep *)s->p - 1)->len;
    if (len == 0)
        return D_006ADF70;
    s->p[len] = 0;
    return s->p;
}
/* a name taken from the value on top of the stack, upper-cased */
static inline void str_name(String *s, Val *v) {
    str_init(s, func_004772A8(v));
    func_00480FE8(s);
}

static inline u32 f2u(f32 f) {
    if (f >= 2147483648.0f)
        return (u32)(s32)(f - 2147483648.0f) | 0x80000000u;
    return (u32)(s32)f;
}

/* drops the scopes that ended before `key`; nonzero while one is left */
static inline int with_active(WithStack *w, u32 key) {
    while (w->top >= 0) {
        if (key < w->e[w->top].end)
            return 1;
        w->top--;
    }
    return 0;
}

#define TOP() (st->top - 1)

/* pops b, then a op= b on the new top */
#define BINOP(fn) do { Val b; func_00476768(&b, TOP()); func_00480F78(st); fn(TOP(), &b); func_004768C0(&b); } while (0)
#define CMPOP(fn) do { Val b; Val *a; func_00476768(&b, TOP()); func_00480F78(st); a = TOP(); \
                       func_00476B30(a, fn(a, &b)); func_004768C0(&b); } while (0)

/* calls the method `name` of `this` without arguments */
static inline void call_this(char *self, const char *name, void *pool, char *ctx) {
    Val obj, ret;
    String s;
    func_00484308(&obj, self);
    str_init(&s, name);
    func_00477C40(&ret, &obj, &s, self, 0, pool, ctx);
    func_004768C0(&ret);
    str_free(&s);
    func_004768C0(&obj);
}

extern "C" Val *func_00481880(Val *result, char *self, u8 *code, u32 len, void *pool, void *table, char *ctx) {
    Val regs[4];
    WithStack with;
    ValStack *st;
    Cursor pc, data;
    u8 *end;
    int i;

    for (i = 0; i < 4; i++)
        regs[i].type = 1;
    for (i = 0; i < 8; i++)
        with.e[i].val.type = 1;
    with.top = -1;
    st = *(ValStack **)(ctx + 0x164);
    pc.p = code;
    end = code + len;

    while (pc.p < end) {
        u32 op = *pc.p++;
        if (op == 0) {
            result->type = 1;
            goto out;
        }
        if (op & 0x80) {
            s32 size = func_00481698(&pc);
            data.p = pc.p;
            pc.p = pc.p + size;
            switch (op) {
            case 0x81: { /* GotoFrame: this.GOTOANDSTOP(frame + 1) */
                Val n, obj, ret;
                String s;
                s32 frame = func_00481698(&data);
                n.type = 6;
                n.v.f = (f32)(frame + 1);
                func_00480F20(st, &n);
                func_00484308(&obj, self);
                str_init(&s, D_006AE070);
                func_00477C40(&ret, &obj, &s, self, 1, pool, ctx);
                func_004768C0(&ret);
                str_free(&s);
                func_004768C0(&obj);
                func_004768C0(&n);
                break;
            }
            case 0x87: { /* StoreRegister */
                u32 r = *data.p++;
                func_00476790(&regs[r], TOP());
                break;
            }
            case 0x88: /* ConstantPool */
                func_00485368(pool, data.p);
                break;
            case 0x94: { /* With */
                Val *obj;
                s32 n;
                with_active(&with, (u32)pc.p);
                obj = TOP();
                n = func_00481698(&data);
                if (with.top < 7) {
                    with.top++;
                    func_00476790(&with.e[with.top].val, obj);
                    with.e[with.top].end = (u32)(pc.p + n);
                }
                func_00480F78(st);
                break;
            }
            case 0x96: /* Push */
                while (data.p < pc.p) {
                    Val v;
                    u32 type;
                    v.type = 1;
                    type = *data.p++;
                    switch (type) {
                    case 0: /* string */
                        func_00476AE8(&v, (const char *)data.p);
                        while (*data.p++ != 0)
                            ;
                        break;
                    case 1: { /* float */
                        union { s32 i; f32 f; } u;
                        u.i = func_004816C0(&data);
                        func_00476B78(&v, u.f);
                        break;
                    }
                    case 2: /* null */
                        func_004769E0(&v);
                        break;
                    case 3: /* undefined */
                        func_004768C0(&v);
                        break;
                    case 4: { /* register */
                        u32 r = *data.p++;
                        func_00476790(&v, &regs[r]);
                        break;
                    }
                    case 5: { /* boolean */
                        u32 b = *data.p++;
                        func_00476B30(&v, b != 0);
                        break;
                    }
                    case 6: { /* double (SWF word order), narrowed to a float bit by bit */
                        union { u8 b[4]; f32 f; } u;
                        u32 c;
                        c = *data.p++;
                        u.b[0] = (u8)(c << 3);
                        u.b[1] = (u8)(c >> 5);
                        c = *data.p++;
                        u.b[2] = (u8)(c >> 5);
                        u.b[1] = (u8)(u.b[1] | (c << 3));
                        c = *data.p++;
                        u.b[3] = (u8)(c >> 5);
                        u.b[2] = (u8)(u.b[2] | ((c & 0x1F) << 3));
                        c = *data.p++;
                        u.b[3] = (u8)(u.b[3] | ((c & 0xC0) | ((c & 7) << 3)));
                        data.p += 3;
                        c = *data.p++;
                        u.b[0] = (u8)(u.b[0] | (c >> 5));
                        func_00476B78(&v, u.f);
                        break;
                    }
                    case 7: /* integer */
                        func_00476BC0(&v, func_004816C0(&data));
                        break;
                    case 8: { /* constant, 8-bit index */
                        u32 k = *data.p++;
                        func_00476AE8(&v, func_00485498(pool, k));
                        break;
                    }
                    case 9: /* constant, 16-bit index */
                        func_00476AE8(&v, func_00485498(pool, func_00481698(&data)));
                        break;
                    default:
                        break;
                    }
                    func_00480F20(st, &v);
                    func_004768C0(&v);
                }
                break;
            case 0x9D: { /* If */
                s32 c = func_004774A8(TOP());
                func_00480F78(st);
                if (c == 0)
                    break;
            }
                /* fall through */
            case 0x99: /* Jump */
                pc.p = pc.p + (short)func_00481698(&data);
                break;
            case 0x9B: { /* DefineFunction */
                String name;
                u8 *n0 = data.p;
                int anon = *n0 == 0;
                void *known = 0;
                str_init(&name, (const char *)n0);
                func_00480FE8(&name);
                if (!anon)
                    known = func_00481280(table, &name);
                if (known) {
                    /* already defined: skip its body */
                    char *f = func_00477298(known, 0, 0);
                    pc.p = pc.p + *(u16 *)(f + 8);
                } else {
                    Val fn;
                    s32 nparams, size2;
                    u8 *params;
                    while (*data.p++ != 0)
                        ;
                    fn.type = 1;
                    nparams = func_00481698(&data);
                    params = data.p;
                    for (i = 0; (u32)i < (u32)nparams; i++)
                        while (*data.p++ != 0)
                            ;
                    size2 = func_00481698(&data);
                    func_00476BE8(&fn, pc.p, size2, nparams, params, table, pool);
                    if (anon)
                        func_00480F20(st, &fn);
                    else
                        func_00476790(func_00481410(table, &name), &fn);
                    pc.p = pc.p + size2;
                    func_004768C0(&fn);
                }
                str_free(&name);
                break;
            }
            default:
                func_0057D9C0(D_006AE080, op, size);
                break;
            }
            continue;
        }

        switch (op) {
        case 0x04: /* NextFrame */
            call_this(self, D_006ADF48, pool, ctx);
            break;
        case 0x05: /* PrevFrame */
            call_this(self, D_006ADF58, pool, ctx);
            break;
        case 0x06: /* Play */
            call_this(self, D_006ADF38, pool, ctx);
            break;
        case 0x07: /* Stop */
            call_this(self, D_006ADF40, pool, ctx);
            break;
        case 0x0A: /* Add */
        case 0x47: /* Add2 */
            BINOP(func_00477DE0);
            break;
        case 0x0B: /* Subtract */
            BINOP(func_00478280);
            break;
        case 0x0C: /* Multiply */
            BINOP(func_004782D0);
            break;
        case 0x0D: /* Divide */
            BINOP(func_00478320);
            break;
        case 0x0E: /* Equals */
        case 0x49: /* Equals2 */
            CMPOP(func_00478178);
            break;
        case 0x10: /* And */
            BINOP(func_004784E0);
            break;
        case 0x12: /* Not */
            func_00478400(TOP());
            break;
        case 0x13: { /* StringEquals */
            Val b;
            Val *a;
            func_00476768(&b, TOP());
            func_004772A8(&b);
            func_00480F78(st);
            func_004772A8(TOP());
            a = TOP();
            func_00476B30(a, func_00478178(a, &b));
            func_004768C0(&b);
            break;
        }
        case 0x17: /* Pop */
            func_00480F78(st);
            break;
        case 0x18: { /* ToInteger */
            Val v;
            func_00476768(&v, TOP());
            func_00480F78(st);
            func_004787B8(&v, 10);
            func_00476B78(&v, (f32)(s32)func_00477460(&v));
            func_00480F20(st, &v);
            func_004768C0(&v);
            break;
        }
        case 0x1C: { /* GetVariable */
            Val target, v;
            String name;
            char *start;
            with_active(&with, (u32)(pc.p - 1));
            target.type = 1;
            func_00484748(self, func_004772A8(TOP()), table, &with, ctx, &start, &target);
            func_00480F78(st);
            str_init(&name, start);
            func_00480FE8(&name);
            if (target.type == 1)
                func_00484530(&v, self, &name, table, &with, ctx);
            else
                func_004774F8(&v, &target, &name);
            func_00480F20(st, &v);
            func_004768C0(&v);
            str_free(&name);
            func_004768C0(&target);
            break;
        }
        case 0x1D: { /* SetVariable */
            Val value, target;
            String name;
            char *start;
            func_00476768(&value, TOP());
            func_00480F78(st);
            with_active(&with, (u32)(pc.p - 1));
            target.type = 1;
            func_00484748(self, func_004772A8(TOP()), table, &with, ctx, &start, &target);
            func_00480F78(st);
            str_init(&name, start);
            func_00480FE8(&name);
            if (target.type == 1) {
                int done = 0;
                if (with.top >= 0) {
                    for (i = with.top; i >= 0; i--) {
                        if (func_00477738(&with.e[i].val, &name, &value, 0)) {
                            done = 1;
                            break;
                        }
                    }
                }
                if (!done) {
                    Val *local = func_00481230(table, &name);
                    if (local)
                        func_00476790(local, &value);
                    else if (!func_004809D8(*(void **)(self + 0x14), &name, &value))
                        func_00476790(func_00481410(self, &name), &value);
                }
            } else {
                func_00477738(&target, &name, &value, 1);
            }
            str_free(&name);
            func_004768C0(&target);
            func_004768C0(&value);
            break;
        }
        case 0x22: { /* GetProperty */
            Val path, target, v;
            const char *p;
            u32 idx = (u32)(s32)func_00477460(TOP());
            func_00480F78(st);
            func_00476768(&path, TOP());
            func_00480F78(st);
            target.type = 1;
            p = func_004772A8(&path);
            if (*p == 0) {
                Val t;
                func_00484308(&t, self);
                func_00476790(&target, &t);
                func_004768C0(&t);
            } else {
                func_0057D9C0(D_006ADFC8, p);
            }
            if (idx < 0x16) {
                String name;
                str_init(&name, D_00624838[idx]);
                func_004774F8(&v, &target, &name);
                func_00480F20(st, &v);
                func_004768C0(&v);
                str_free(&name);
            } else {
                v.type = 1;
                func_00480F20(st, &v);
                func_004768C0(&v);
            }
            func_004768C0(&target);
            func_004768C0(&path);
            break;
        }
        case 0x23: { /* SetProperty */
            Val value, path, target;
            const char *p;
            u32 idx;
            func_00476768(&value, TOP());
            func_00480F78(st);
            idx = (u32)(s32)func_00477460(TOP());
            func_00480F78(st);
            func_00476768(&path, TOP());
            func_00480F78(st);
            target.type = 1;
            p = func_004772A8(&path);
            if (*p == 0) {
                Val t;
                func_00484308(&t, self);
                func_00476790(&target, &t);
                func_004768C0(&t);
            } else {
                func_0057D9C0(D_006AE000, p);
            }
            if (idx < 0x16) {
                String name;
                str_init(&name, D_00624838[idx]);
                func_00477738(&target, &name, &value, 0);
                str_free(&name);
            }
            func_004768C0(&target);
            func_004768C0(&path);
            func_004768C0(&value);
            break;
        }
        case 0x26: /* Trace */
            func_0057D9C0(D_006ADF68, func_004772A8(TOP()));
            func_00480F78(st);
            break;
        case 0x34: { /* GetTime: milliseconds */
            Val v;
            f32 t = func_00475DA8(ctx);
            v.type = 6;
            v.v.f = (f32)(s32)(t * 1000.0f);
            func_00480F20(st, &v);
            func_004768C0(&v);
            break;
        }
        case 0x3C: { /* DefineLocal */
            Val value;
            String name;
            func_00476768(&value, TOP());
            func_00480F78(st);
            str_name(&name, TOP());
            func_00480F78(st);
            func_00476790(func_00481410(table, &name), &value);
            str_free(&name);
            func_004768C0(&value);
            break;
        }
        case 0x3D: { /* CallFunction */
            String name;
            u32 nargs;
            Val v;
            int found = 0;
            str_name(&name, TOP());
            func_00480F78(st);
            nargs = f2u(func_00477460(TOP()));
            func_00480F78(st);
            if (with_active(&with, (u32)(pc.p - 1))) {
                /* a method of a `with` scope, innermost first */
                Val r, t;
                r.type = 1;
                for (i = with.top; i >= 0; i--) {
                    func_00477C40(&t, &with.e[i].val, &name, self, nargs, pool, ctx);
                    func_00476790(&r, &t);
                    func_004768C0(&t);
                    if (r.type != 2)
                        break;
                }
                if (i < 0)
                    func_00476A08(&r);
                func_00476768(&v, &r);
                func_004768C0(&r);
                if (v.type != 2) {
                    func_00480F20(st, &v);
                    func_004768C0(&v);
                    found = 1;
                } else {
                    func_004768C0(&v);
                }
            }
            if (!found) {
                Val *local = func_00481230(table, &name);
                if (local && local->type == 13) {
                    func_00477890(&v, local, self, nargs, pool, ctx);
                    func_00480F20(st, &v);
                    func_004768C0(&v);
                } else {
                    Val obj, t;
                    v.type = 1;
                    func_00484308(&obj, self);
                    func_00477C40(&t, &obj, &name, self, nargs, pool, ctx);
                    func_00476790(&v, &t);
                    func_004768C0(&t);
                    func_004768C0(&obj);
                    if (v.type != 2) {
                        func_00480F20(st, &v);
                        func_004768C0(&v);
                    } else {
                        func_00476698(&t, &name, ctx);
                        func_00476790(&v, &t);
                        func_004768C0(&t);
                        if (v.type == 1) {
                            func_00481190(&t, func_00475D98(ctx), &name, nargs, st);
                            func_00480F20(st, &t);
                            func_004768C0(&t);
                        } else {
                            func_00477890(&t, &v, self, nargs, pool, ctx);
                            func_00480F20(st, &t);
                            func_004768C0(&t);
                        }
                        func_004768C0(&v);
                    }
                }
            }
            str_free(&name);
            break;
        }
        case 0x3E: { /* Return */
            Val v;
            func_00476768(&v, TOP());
            func_00480F78(st);
            func_00476768(result, &v);
            func_004768C0(&v);
            goto out_returned;
        }
        case 0x3F: /* Modulo */
            BINOP(func_00478378);
            break;
        case 0x40: { /* NewObject */
            String name;
            u32 nargs;
            Val ctor, v;
            str_init(&name, func_004772A8(TOP()));
            func_0057D9C0(D_006ADF78, str_c(&name));
            func_00480FE8(&name);
            func_00480F78(st);
            nargs = f2u(func_00477460(TOP()));
            func_00480F78(st);
            func_00484530(&ctor, self, &name, table, &with, ctx);
            if (ctor.type != 1) {
                v.type = 1;
                func_00476D40(&v, &ctor, self, nargs, pool, ctx);
            } else {
                func_0057D9C0(D_006ADF88, str_c(&name));
                func_00480FA0(st, nargs);
                v.type = 1;
            }
            func_00480F20(st, &v);
            func_004768C0(&v);
            func_004768C0(&ctor);
            str_free(&name);
            break;
        }
        case 0x41: { /* DefineLocal2 */
            String name;
            str_name(&name, TOP());
            func_00481410(table, &name);
            func_00480F78(st);
            str_free(&name);
            break;
        }
        case 0x42: { /* InitArray */
            Val arr, v;
            s32 n = (s32)func_00477460(TOP());
            func_00480F78(st);
            arr.type = 1;
            func_00476A38(&arr);
            v.type = 1;
            func_00476D40(&v, &arr, self, n, pool, ctx);
            func_00480F20(st, &v);
            func_004768C0(&v);
            func_004768C0(&arr);
            break;
        }
        case 0x43: { /* InitObject */
            Val proto, obj;
            s32 n = (s32)func_00477460(TOP());
            func_00480F78(st);
            proto.type = 1;
            obj.type = 1;
            func_00476D40(&obj, &proto, self, 0, pool, ctx);
            if (n > 0) {
                s32 k = n;
                do {
                    Val value;
                    String name;
                    func_00476768(&value, TOP());
                    func_00480F78(st);
                    str_name(&name, TOP());
                    func_00480F78(st);
                    func_00477738(&obj, &name, &value, 1);
                    str_free(&name);
                    func_004768C0(&value);
                } while (--k != 0);
            }
            func_00480F20(st, &obj);
            func_004768C0(&obj);
            func_004768C0(&proto);
            break;
        }
        case 0x44: { /* TypeOf */
            Val *a = TOP();
            func_00476AE8(a, func_00478A18(a));
            break;
        }
        case 0x48: /* Less2 */
            CMPOP(func_00477F28);
            break;
        case 0x4A: { /* ToNumber */
            Val v;
            func_00476768(&v, TOP());
            func_00480F78(st);
            if (v.type == 7) {
                Val r, n;
                String s;
                str_init(&s, D_006AE038);
                func_00477C40(&r, &v, &s, self, 0, pool, ctx);
                str_free(&s);
                n.type = 6;
                n.v.f = func_00477460(&r);
                func_00480F20(st, &n);
                func_004768C0(&n);
                func_004768C0(&r);
            } else {
                func_004787B8(&v, 10);
                func_00480F20(st, &v);
            }
            func_004768C0(&v);
            break;
        }
        case 0x4B: { /* ToString */
            Val v;
            int done = 0;
            func_00476768(&v, TOP());
            func_00480F78(st);
            if (v.type == 7) {
                Val r;
                String s;
                str_init(&s, D_006AE040);
                func_00477C40(&r, &v, &s, self, 0, pool, ctx);
                str_free(&s);
                if ((u32)(r.type - 3) < 2) {
                    func_00480F20(st, &r);
                    done = 1;
                }
                func_004768C0(&r);
            }
            if (!done) {
                func_004772A8(&v);
                func_00480F20(st, &v);
            }
            func_004768C0(&v);
            break;
        }
        case 0x4C: /* PushDuplicate */
            func_00480F20(st, TOP());
            break;
        case 0x4D: { /* StackSwap */
            Val a, b;
            func_00476768(&a, TOP());
            func_00480F78(st);
            func_00476768(&b, TOP());
            func_00480F78(st);
            func_00480F20(st, &a);
            func_00480F20(st, &b);
            func_004768C0(&b);
            func_004768C0(&a);
            break;
        }
        case 0x4E: { /* GetMember */
            String name;
            Val obj, v;
            str_name(&name, TOP());
            func_00480F78(st);
            func_00476768(&obj, TOP());
            func_00480F78(st);
            func_004774F8(&v, &obj, &name);
            func_00480F20(st, &v);
            func_004768C0(&v);
            func_004768C0(&obj);
            str_free(&name);
            break;
        }
        case 0x4F: { /* SetMember */
            Val value, obj;
            String name;
            func_00476768(&value, TOP());
            func_00480F78(st);
            str_name(&name, TOP());
            func_00480F78(st);
            func_00476768(&obj, TOP());
            func_00480F78(st);
            func_00477738(&obj, &name, &value, 1);
            func_004768C0(&obj);
            str_free(&name);
            func_004768C0(&value);
            break;
        }
        case 0x50: /* Increment */
            func_00478748(TOP());
            break;
        case 0x51: /* Decrement */
            func_00478780(TOP());
            break;
        case 0x52: { /* CallMethod */
            String name;
            Val obj, v;
            u32 nargs;
            str_name(&name, TOP());
            func_00480F78(st);
            func_00476768(&obj, TOP());
            func_00480F78(st);
            nargs = f2u(func_00477460(TOP()));
            func_00480F78(st);
            func_00477C40(&v, &obj, &name, self, nargs, pool, ctx);
            func_00480F20(st, &v);
            func_004768C0(&v);
            func_004768C0(&obj);
            str_free(&name);
            break;
        }
        case 0x53: { /* NewMethod */
            String name;
            Val obj, ctor, v;
            u32 nargs;
            str_init(&name, func_004772A8(TOP()));
            func_0057D9C0(D_006ADFB8, str_c(&name));
            func_00480FE8(&name);
            func_00480F78(st);
            func_00476768(&obj, TOP());
            func_00480F78(st);
            nargs = f2u(func_00477460(TOP()));
            func_00480F78(st);
            func_004774F8(&ctor, &obj, &name);
            if (ctor.type != 1) {
                v.type = 1;
                func_00476D40(&v, &ctor, self, nargs, pool, ctx);
            } else {
                func_0057D9C0(D_006ADF88, str_c(&name));
                func_00480FA0(st, nargs);
                v.type = 1;
            }
            func_00480F20(st, &v);
            func_004768C0(&v);
            func_004768C0(&ctor);
            func_004768C0(&obj);
            str_free(&name);
            break;
        }
        case 0x55: { /* Enumerate2 */
            Val obj, n;
            func_00476768(&obj, TOP());
            func_00480F78(st);
            n.type = 1;
            func_004769E0(&n);
            func_00480F20(st, &n);
            func_00477D98(&obj, st);
            func_004768C0(&n);
            func_004768C0(&obj);
            break;
        }
        case 0x60: /* BitAnd */
            BINOP(func_00478480);
            break;
        case 0x61: /* BitOr */
            BINOP(func_00478540);
            break;
        case 0x62: /* BitXor */
            BINOP(func_004785A0);
            break;
        case 0x63: /* BitLShift */
            BINOP(func_00478600);
            break;
        case 0x64: /* BitRShift */
            BINOP(func_00478660);
            break;
        case 0x65: /* BitURShift */
            BINOP(func_004786C0);
            break;
        case 0x66: /* StrictEquals */
            CMPOP(func_00478250);
            break;
        case 0x67: /* Greater */
            CMPOP(func_00478000);
            break;
        case 0x68: /* StringGreater */
            CMPOP(func_004780F8);
            break;
        default:
            func_0057D9C0(D_006AE050, op);
            break;
        }
    }
    result->type = 1;
out:
out_returned:
    for (i = 7; i >= 0; i--)
        func_004768C0(&with.e[i].val);
    for (i = 3; i >= 0; i--)
        func_004768C0(&regs[i]);
    return result;
}
