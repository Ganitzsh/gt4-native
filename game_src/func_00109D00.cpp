/* compiler: ee-gcc2.96-no-strict-aliasing */
/* MenuGameObject::startProject (00109D00, matched in gt4-decomp
 * src/startup/unit_00101400/GranTurismo4__MenuGameObject__startProject.cpp): loads a menu project and runs it until
 * its event loop ends. Both ways into GT4's Options come here with "option": sequence 1 for the top menu's Options,
 * func_0010FE40 for a quick menu's (M0). gt4-native adds one call once the project runs (MENU_DESIGN.md 3.2, M6):
 * gt4_menu_entry (runtime/menu/menu_host.h), which with ui.entry = integrated opens the settings hub over GT4's
 * Options; the menu update's copy (func_00109B38.cpp) ends the project when the hub closes without handing over to it.
 *
 * Shared project names and replay-mode table retain their original pool addresses.
 * The local table copy preserves the launcher's stack-owned iteration state. */
extern const char *const replayModeNames[5] __asm__("D_0068CEB8");
extern "C" void *memcpy(void *, const void *, unsigned int);
extern const char emptyProjectName[] __asm__("D_0068CE00");
extern const char gtModeProject[] __asm__("D_0068CE08");
extern const char optionProject[] __asm__("D_0068CE10");
extern const char settingProject[] __asm__("D_0068CE18");
extern const char photoSaveProject[] __asm__("D_0068CE20");
extern const char printProject[] __asm__("D_0068CE30");
extern const char laboProject[] __asm__("D_0068CE38");
extern const char projectPathFormat[] __asm__("D_0068CED0");

typedef int s32;
typedef unsigned int u32;

struct StringRep { u32 len; u32 res; u32 ref; u32 selfish; };
struct S00659988 { const char *name; };

struct G {
    char pad0[8];
    s32 mode;
    char padC[0x94 - 0xC];
    char name[0x40];
};
struct GPtr { char *p; };

struct VEntry {
    short delta;
    short index;
    void (*fn)(void *);
};
struct VEntry1 {
    short delta;
    short index;
    void (*fn)(void *, void *);
};
struct VEntry2 {
    short delta;
    short index;
    void (*fn)(void *, s32, s32);
};

struct Proj {
    char pad0[4];
    char *vtbl;
    char pad8[0xD8 - 8];
    s32 fD8;
    s32 fDC;
    char padE0[4];
    s32 fE4;
};

struct Self {
    char pad0[0x64];
    char *vtbl;
    char pad68[8];
    s32 f70;
    char f74[4];
};

extern StringRep D_00659FA8;
extern GPtr D_00622F4C;

extern "C" void func_00109348(void);
extern "C" int func_00109398(void);
extern "C" void func_00109C40(void);
extern "C" void func_00109C70(Self *self);
extern "C" void func_00109CB8(Self *);
extern "C" void func_00109CE0(Self *);
extern "C" void func_0010AF10(s32);
extern "C" void func_0010AF40(int);
extern "C" void func_00211408(void *, int);
extern "C" void *func_002117A0(void *);
extern "C" Proj *func_00213158(s32);
struct String;
extern "C" int MWidgetReader__structor_2(s32, void *, const String &, s32);
extern "C" void func_00219030(s32, void *, s32);
extern "C" void mManager__setupMemory(s32, s32);
extern "C" void func_00219420(s32);
extern "C" void func_00219570(s32, s32);
extern "C" void func_00219678(s32);
extern "C" void func_00229270(void *, const s32 *);
extern "C" void func_002292A0(void *, int);
extern "C" s32 func_0022EFB8(s32);
extern "C" void func_0022F990(s32, s32);
extern "C" void func_0023DDF0(void *, int);
extern "C" void func_0023FB80(void *);
extern "C" void func_00251998(Proj *, s32);
extern "C" s32 func_00251BD0(Proj *);
extern "C" s32 func_00251BD8(Proj *, s32);
extern "C" s32 func_00251BE8(Proj *, s32);
extern "C" void func_00251BF8(Proj *, s32, s32);
extern "C" void func_002EA590(void *, int);
extern "C" void func_002ECDE0(void *);
extern "C" void func_00326798(void *, s32, s32, const char *);
extern "C" int func_00327908(void);
extern "C" void func_00327928(s32);
extern "C" void func_003285A8(Proj *);
extern "C" void func_003285F8(Proj *);
extern "C" void func_0042FCE0(G *, const char *);
extern "C" void func_00575DA0(s32);
extern "C" void func_00576788(void *);
extern "C" void func_005767C0(void *);
extern "C" s32 func_00578CB0(s32);
extern "C" void func_0057DA20(char *, const char *, ...);
extern "C" int func_0057F238(const char *, const char *);
extern "C" u32 func_0057F260(const char *);
extern "C" S00659988 *func_005C11A8(void);
extern "C" char *func_005C2560(StringRep *);
extern "C" void *func_005C2630(void *, u32, u32, const char *, u32);
extern "C" void gt4_menu_entry(unsigned int project);
extern "C" void gt4_menu_started(unsigned int project, const char *name);

static inline char *grab(StringRep *r) {
    if (r->selfish)
        return func_005C2560(r);
    ++r->ref;
    return (char *)(r + 1);
}

struct String {
    char *dat;
    char pad[0xC];
    String(const char *s) {
        dat = grab(&D_00659FA8);
        func_005C2630(this, 0, (u32)-1, s, func_0057F260(s));
    }
    ~String() {
        StringRep *q = (StringRep *)(dat - 0x10);
        if (--q->ref == 0) {
            s32 size = q->res + 0x10;
            func_00326798(q, size, 4, func_005C11A8()->name);
        }
    }
};

struct Ref {
    Proj *p;
    char pad[0xC];
    Ref(const s32 &v) { func_00229270(this, &v); }
    ~Ref() { func_002292A0(this, 2); }
    Ref &operator=(const Ref &o) {
        if (this != &o) {
            Proj *n = o.p;
            if (n != 0)
                func_003285A8(n);
            Proj *old = p;
            if (old != 0)
                func_003285F8(old);
            p = n;
        }
        return *this;
    }
};

struct Ctx { s32 a[4]; Ctx() { func_002ECDE0(this); } ~Ctx() { func_002EA590(this, 2); } };
struct Mgr { s32 p; s32 a[3]; Mgr() { func_002117A0(this); } ~Mgr() { func_00211408(this, 2); } };
struct Scr { s32 a[4]; Scr() { func_0023FB80(this); } ~Scr() { func_0023DDF0(this, 2); } };

static inline G *game(GPtr *g) { return (G *)(g->p + 0x3A368); }

extern "C" s32 GranTurismo4__MenuGameObject__startProject(Self *self, const char *name, s32 arg2) {
    /* gt4-native: GT4's Options (`name` is next-project's buffer on sequence 1, which the project itself rewrites) */
    bool option = func_0057F238(name, optionProject) == 0;
    bool started = true;   /* gt4-native: the project's name to the runtime at its first run (M9d2) */
    func_0042FCE0(game(&D_00622F4C), emptyProjectName);
    int was = func_00109398();
    if (!was)
        func_0010AF40(0);
    Ctx ctx;
    Mgr mgr;
    Mgr *pm = &mgr;
    {
        VEntry *e = (VEntry *)(self->vtbl + 0x10);
        e->fn((char *)self + e->delta);
    }
    G *g = game(&D_00622F4C);
    s32 mem;
    switch (g->mode) {
    case 1:
        mem = 1;
        break;
    case 17:
    case 18:
    case 19:
        mem = 2;
        break;
    case 20:
        mem = 3;
        break;
    case 21:
    case 22:
        mem = 2;
        break;
    default:
        mem = 3;
        break;
    }
    if (func_0057F238(name, gtModeProject) == 0) {
        mem = 0;
    } else if (func_0057F238(name, optionProject) == 0 || func_0057F238(name, settingProject) == 0) {
        mem = 1;
    } else if (func_0057F238(name, photoSaveProject) == 0 || func_0057F238(name, printProject) == 0) {
        mem = 3;
    } else if (func_0057F238(name, laboProject) == 0) {
        const char *modes[5];
        memcpy(modes, replayModeNames, sizeof modes);
        const char *cur = g->name;
        for (const char **m = modes; *m; m++) {
            if (func_0057F238(cur, *m) == 0) {
                mem = 2;
                break;
            }
        }
    }
    mManager__setupMemory(pm->p, mem);
    {
    Scr scr;
    s32 buf = func_00578CB0(0x8000);
    func_00219570(pm->p, arg2);
    {
    Ref root(0);
    char path[0x80];
    func_0057DA20(path, projectPathFormat, name, name);
    {
        s32 rd = pm->p;
        MWidgetReader__structor_2(rd, &root, String(path), 0);
    }
    if (was)
        func_00109348();
    Proj *proj = func_00213158(pm->p);
    do {
        s32 saved = func_00327908();
        {
            VEntry *e = (VEntry *)(proj->vtbl + 0x210);
            e->fn((char *)proj + e->delta);
        }
        {
            Ref *pr = &root;
            {
                Proj *o = pr->p;
                VEntry1 *e = (VEntry1 *)(o->vtbl + 0x2B0);
                e->fn((char *)o + e->delta, proj);
            }
            {
                Proj *o = pr->p;
                VEntry1 *e = (VEntry1 *)(o->vtbl + 0x2C0);
                e->fn((char *)o + e->delta, proj);
            }
        }
        proj->fD8 = (s32)func_00109C40;
        proj->fDC = (s32)func_00109C70;
        if (func_00251BD0(proj) <= 0)
            func_00251998(proj, 1);
        s32 page = func_00251BD8(proj, 0);
        s32 focus = func_00251BE8(proj, 0);
        if (focus) {
            func_0022F990(page, focus);
            func_00251BF8(proj, 0, 0);
        }
        if (func_0022EFB8(page) > 0) {
            func_00576788(self->f74);
            self->f70 = (s32)proj;
            func_005767C0(self->f74);
        }
        bool has = self->f70 != 0;
        func_0010AF10(1);
        if (has) {
            if (started) {   /* gt4-native: which project runs (M9d2: the opening's boot waits for command 6) */
                started = false;
                gt4_menu_started((unsigned int)self->f70, name);
            }
            if (option) {   /* gt4-native: once, at the project's first run */
                option = false;
                gt4_menu_entry((unsigned int)self->f70);
            }
            {
                Proj *o = (Proj *)self->f70;
                VEntry2 *e = (VEntry2 *)(o->vtbl + 0x190);
                e->fn((char *)o + e->delta, buf, 0x8000);
            }
            while (((Proj *)self->f70)->fE4 == 0)
                func_00109CE0(self);
            {
                Proj *o = (Proj *)self->f70;
                VEntry *e = (VEntry *)(o->vtbl + 0x198);
                e->fn((char *)o + e->delta);
            }
        }
        func_00109CB8(self);
        if (self->f70) {
            ((Proj *)self->f70)->fD8 = 0;
            ((Proj *)self->f70)->fDC = 0;
        }
        func_00576788(self->f74);
        self->f70 = 0;
        func_005767C0(self->f74);
        {
            Ref *pr = &root;
            {
                Proj *o = pr->p;
                VEntry1 *e = (VEntry1 *)(o->vtbl + 0x2C8);
                e->fn((char *)o + e->delta, proj);
            }
            {
                Proj *o = pr->p;
                VEntry1 *e = (VEntry1 *)(o->vtbl + 0x2B8);
                e->fn((char *)o + e->delta, proj);
            }
        }
        {
            VEntry *e = (VEntry *)(proj->vtbl + 0x218);
            e->fn((char *)proj + e->delta);
        }
        func_00327928(saved);
    } while (func_00251BE8(proj, 0));
    func_00219030(pm->p, &root, 0);
    root = Ref(0);
    func_00219678(pm->p);
    func_00575DA0(buf);
    }
    }
    func_00219420(pm->p);
    {
        VEntry *e = (VEntry *)(self->vtbl + 0x20);
        e->fn((char *)self + e->delta);
    }
    return 0;
}
