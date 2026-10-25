/* native stand-in, not matched: builds the script list of the network's entries: for each of the
   D_00645570 entries (count at +0x5A8 -> +0x5B30), a row of 12 values (ints and strings, each taken
   from the entry when its flag bit is set, else a default: 9999, -1 or D_00696C68) appended to a
   new list; *ret = that list. Written from the opus draft (build/swarm/o1/001f1738.cpp, structurally
   identical to the original, 22 instructions off once aligned: register choice) and checked against
   the assembly. */
typedef int s32;
typedef unsigned int u32;

struct Handle {
    void *p;
    char pad[0xC];
};

struct StringRep { u32 len; u32 res; u32 ref; u32 selfish; };
struct String { char *dat; char pad[0xC]; };
struct S00659988 { const char *name; };

extern StringRep D_00659FA8;
extern char D_00645570[];
extern char D_00696C68[];

extern "C" char *func_005C2560(StringRep *r, int unused0);
extern "C" String *func_005C2630(String *str, u32 pos, u32 n1, const char *s, u32 n2);
extern "C" s32 func_0057F260(const char *s);
extern "C" S00659988 *func_005C11A8(int unused0);
extern "C" void func_00326798(void *p, s32 size, s32 align, const char *name);

extern "C" void func_002EFBF0(void *h);                       /* new list handle */
extern "C" Handle *func_002EEEB0(Handle *ret, void *self, Handle *x); /* list append */
extern "C" void func_00309378(void *h, s32 how, int unused0);
extern "C" void func_002ED5C0(void *h, int how);
extern "C" void func_002ED5A8(void *dst, void *src);
extern "C" void func_002FE278(void *h, void *val);            /* int handle */
extern "C" void func_002FC870(void *h, int how);
extern "C" void func_00314B20(void *h, void *str);            /* string handle */
extern "C" void func_00312318(void *h, int how);

struct Entry {
    char pad0[0x1C];
    u32 flags;
    s32 x20;
    char s24[0x80];
    char sA4[0x80];
    s32 x124;
    char s128[0x100];
    s32 x228;
    s32 x22C;
    s32 x230;
    char s234[8];
    char s23C[0x80];
    char s2BC[0x80];
    char s33C[0x80];
};

extern "C" char *func_004F6CF0(void *s, int i);

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

static inline void add_str(void *row, const char *text) {
    String s;
    Handle v, tmp;
    construct(&s, text);
    func_00314B20(&v, &s);
    func_002EEEB0(&tmp, row, &v);
    func_00309378(&tmp, 2, 0);
    func_00312318(&v, 2);
    release(&s);
}

static inline void add_int(void *row, s32 x) {
    Handle v, tmp;
    func_002FE278(&v, (void *)x);
    func_002EEEB0(&tmp, row, &v);
    func_00309378(&tmp, 2, 0);
    func_002FC870(&v, 2);
}

extern "C" Handle *func_001F1738(Handle *ret) {
    Handle list;
    Handle row;
    Handle tmp;
    int count;
    int i;

    func_002EFBF0(&list);
    count = *(int *)(*(char **)(D_00645570 + 0x5A8) + 0x5B30);
    for (i = 0; i < count; i++) {
        Entry *e = (Entry *)func_004F6CF0(D_00645570, i);
        func_002EFBF0(&row);
        add_int(row.p, (e->flags & 4) ? e->x20 : 0x270F);
        add_str(row.p, (e->flags & 8) ? e->s24 : D_00696C68);
        add_str(row.p, (e->flags & 0x10) ? e->sA4 : D_00696C68);
        add_int(row.p, (e->flags & 0x10) ? e->x124 : -1);
        add_str(row.p, (e->flags & 0x20) ? e->s128 : D_00696C68);
        add_int(row.p, (e->flags & 0x40) ? e->x228 : -1);
        add_int(row.p, (e->flags & 0x40) ? e->x22C : -1);
        add_int(row.p, (e->flags & 0x40) ? e->x230 : -1);
        add_str(row.p, (e->flags & 0x80) ? e->s234 : D_00696C68);
        add_str(row.p, (e->flags & 0x80) ? e->s23C : D_00696C68);
        add_str(row.p, (e->flags & 0x100) ? e->s2BC : D_00696C68);
        add_str(row.p, (e->flags & 0x200) ? e->s33C : D_00696C68);
        func_002EEEB0(&tmp, list.p, &row);
        func_00309378(&tmp, 2, 0);
        func_002ED5C0(&row, 2);
    }
    func_002ED5A8(ret, &list);
    func_002ED5C0(&list, 2);
    return ret;
}
