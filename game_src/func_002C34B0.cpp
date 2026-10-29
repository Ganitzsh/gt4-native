/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native copy of the provisional draft for 002C34B0: the case-4 float getter gets n and args, which the EE forwards to it in $a2 and $a3 ($a1 then holds the object) (passthrough_lint) */
typedef int s32;
typedef short s16;

struct Rep { s32 len; s32 cap; s32 ref; s32 sel; };
struct S00659988 { const char *name; };
struct Str { char *p; char pad[0xC]; };
struct VEntry { s16 delta; s16 index; void (*fn)(void *, void *); };
struct VEntI { s16 delta; s16 index; s32 (*fn)(void *); };
struct VEntF { s16 delta; s16 index; float (*fn)(void *); };
struct VObj { char pad0[4]; char *vtbl; };
struct H { void *o; char pad[0xC]; };
union U { Str s; H h; };

extern char D_0069C550[];

extern "C" void func_002C2878(void *, int) throw();
extern "C" void func_002C28D0(void *, void *) throw();
extern "C" void func_002C3CF8(void *, const char *, s32, s32, float);
extern "C" void func_00326798(void *, s32, s32, const char *);
extern "C" S00659988 *func_005C11A8(void);

static inline Rep *str_rep(Str *s) { return (Rep *)s->p - 1; }
static inline char *rep_data(Rep *r) { return (char *)(r + 1); }
static inline s32 str_length(Str *s) { return str_rep(s)->len; }
static inline void str_terminate(Str *s) { rep_data(str_rep(s))[str_length(s)] = 0; }
static inline const char *c_str(Str *s) {
    if (str_length(s) == 0) return D_0069C550;
    str_terminate(s);
    return rep_data(str_rep(s));
}
static inline void release(Str *s) {
    Rep *q = str_rep(s);
    if (--q->ref == 0) { s32 size = q->cap + 0x10; func_00326798(q, size, 4, func_005C11A8()->name); }
}

extern "C" void func_002C34B0(void *self, void *arg1, s32 n, VObj **args) {
    s32 s3 = -1;
    s32 s5 = 0;
    float f = 1.0f;
    U u;
    switch (n) {
    case 4: {
        VObj *o = args[3];
        VEntF *e = (VEntF *)(o->vtbl + 0x60);
        f = ((float (*)(void *, VObj *, s32, VObj **))e->fn)((char *)o + e->delta, o, n, args);
    }
    case 3: {
        VObj *o = args[2];
        VEntI *e = (VEntI *)(o->vtbl + 0x58);
        s5 = e->fn((char *)o + e->delta) != 0;
    }
    case 2: {
        VObj *o = args[1];
        VEntI *e = (VEntI *)(o->vtbl + 0x58);
        s3 = e->fn((char *)o + e->delta);
    }
    case 1: {
        VObj *o = args[0];
        VEntry *e = (VEntry *)(o->vtbl + 0x18);
        e->fn(&u.s, (char *)o + e->delta);
        const char *cs = c_str(&u.s);
        release(&u.s);
        func_002C28D0(&u.h, arg1);
        func_002C3CF8(u.h.o, cs, s3, s5, f);
        func_002C2878(&u.h, 2);
    }
    }
}
