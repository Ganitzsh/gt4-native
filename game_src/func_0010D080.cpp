/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native correction of the provisional draft drafts/0010d080/0010d080_519100d364489b18.cpp (argument widths, argcheck.py width/retwidth): RaceLanBattleInformation__virtual_03 takes `s64 arg1`; the draft declared s32, so natively the callee read the id's high half from the next stack slot. */
typedef int s32;
typedef unsigned int u32;
typedef short s16;

struct VEntryI { short delta; short index; void (*fn)(void *, s32); };

struct Course {
    char pad0[0x974];
    char *vtbl;
};

struct Run {
    char pad0[0x64];
    char *vtbl;
    char pad68[4];
    Course *u6C;
    char pad70[0xE44C - 0x70];
    s32 fE44C;
};

struct Tri { s32 a; s32 b; s32 c; };
struct Cfg { s32 w0; s32 w1; s32 w2; s32 w3; s32 w4; s32 w5; };

struct Info {
    char pad0[0xB0];
    Tri fB0;
    Tri fBC;
    s32 fC8;
    s32 fCC;
    char padD0[0x120 - 0xD0];
    s32 f120;
    char pad124[0xAD0 - 0x124];
};

struct Net {
    char pad0[0xF98];
    Cfg fF98;
    char padFB0[0x3F44 - 0xFB0];
    s32 f3F44;
};

struct Game {
    char pad0[4];
    char f4[0x44 - 4];
    char f44[0x3A344 - 0x44];
    s16 f3A344;
};

struct Obj { char b[0x70]; };

extern Game *D_00622F4C;
extern Net D_00645570;

extern "C" void func_0010AF40(int);
extern "C" void func_0010CF08(Run *) throw();
extern "C" void func_0010CFB8(Obj *, char *) throw();
extern "C" Info *RaceLanBattleInformation__structor_0(Info *);
extern "C" void RaceLanBattleInformation__structor_1(Info *, s32);
extern "C" void RaceLanBattleInformation__virtual_03(Info *, long long);
extern "C" void ResultLinkBattle__structor_0(Run *);
extern "C" void func_0035F3D8(void);
extern "C" Course *func_003B7850(Info *);
extern "C" void func_003BB440(Info *, char *);
extern "C" void RaceInformation__setEntryCars(Info *, s32);
extern "C" void func_00430750(Obj *);
extern "C" void func_004307A0(Obj *, s32);
extern "C" s32 func_00447C78(char *);
extern "C" void func_0044CA98(int, int, int, int);
extern "C" void func_0044CB10(void);
extern "C" void func_004F83E8(Net *);
extern "C" void func_004F8770(Net *);
extern "C" char *func_004FA1A8(Net *, s32);
extern "C" s32 func_004FAEE8(Net *);
extern "C" void *exception__structor_0(u32);
extern "C" void func_005C2A10(Info *, s32, Obj *);

#define VCALLI(r, off, arg) { VEntryI *e = (VEntryI *)((r)->vtbl + (off)); e->fn((char *)(r) + e->delta, (arg)); }

extern "C" void func_0010D080(void) {
    func_0035F3D8();
    if (func_004FAEE8(&D_00645570)) {
        Info info;
        Cfg cfg;
        Obj obj;
        s32 i;

        func_0044CA98(0x30000, 2, 6, 0xBC8000);
        func_004F83E8(&D_00645570);
        RaceLanBattleInformation__structor_0(&info);
        func_003BB440(&info, D_00622F4C->f44);
        RaceLanBattleInformation__virtual_03(&info, func_00447C78(D_00622F4C->f4));
        s32 n = D_00645570.f3F44;
        cfg = D_00645570.fF98;
        Tri *t = &info.fB0;
        t->a = cfg.w0;
        info.fB0.c = cfg.w3;
        info.fB0.b = cfg.w5;
        info.fBC = *t;
        info.fC8 = cfg.w1;
        info.fCC = cfg.w2;
        s32 flag = cfg.w4 != 0;
        RaceInformation__setEntryCars(&info, n);
        if (D_00645570.f3F44 < 2) {
            info.f120 = D_00622F4C->f3A344;
        }
        for (i = 0; i < n; i++) {
            char *p = func_004FA1A8(&D_00645570, i);
            func_00430750(&obj);
            func_0010CFB8(&obj, p);
            func_005C2A10(&info, i, &obj);
            func_004307A0(&obj, 2);
        }
        Course *course = func_003B7850(&info);
        Run *run = (Run *)exception__structor_0(0x239C0);
        ResultLinkBattle__structor_0(run);
        run->fE44C = flag;
        run->u6C = course;
        func_0010CF08(run);
        if (run) {
            VCALLI(run, 8, 3)
        }
        func_0010AF40(0);
        if (course) {
            VEntryI *e = (VEntryI *)(course->vtbl + 8);
            e->fn((char *)course + e->delta, 3);
        }
        func_004F8770(&D_00645570);
        RaceLanBattleInformation__structor_1(&info, 2);
        func_0044CB10();
    }
}
