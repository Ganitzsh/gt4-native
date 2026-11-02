/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native copy of 00119918 (matched): fills the race's quick work from the cars and the race information. Not the
 * original's source, the original's behaviour: the information's virtual at +0x10 returns the race id, 64 bits
 * (00118EF0 reads it so, getl), which the matched source declares as s32 and passes on to 00447CA0(long id, ...): on
 * the EE the whole register goes through, natively only the low word did, the race code lookup (00443E60) missed and
 * the quick work's race label stayed empty. A championship resumed from the GT mode map then had no race code, and
 * its race entry screen crashed in MRenderContext::translate (or the game hung). */
typedef int s32;
typedef float f32;

struct VEntry { short delta; short index; s32 (*fn)(void *); };
struct VEntryI { short delta; short index; s32 (*fn)(void *, s32); };
struct VEntryL { short delta; short index; long (*fn)(void *); };   /* long: 64 bits, the EE's */

struct Car { char pad0[0x3444]; s32 f3444; char pad3448[0x3460 - 0x3448]; s32 f3460; };
struct List { s32 count; char pad4[4]; Car **p8; };
struct Course {
    s32 f0;
    char pad4[0x60 - 4];
    List *p60;
    char pad64[0x70 - 0x64];
    void *p70;
    char pad74[0x80 - 0x74];
    void *p80;
    char pad84[0x970 - 0x84];
    s32 f970;
};
struct W { char pad[0x6C]; Course *u6C; };
struct Run { char pad0[0x12C]; char *vtbl; char pad130[0x170 - 0x130]; s32 f170; };
struct Self {
    char pad0[0x46C];
    s32 f46C;
    char pad470[0x478 - 0x470];
    s32 f478;
    char pad47C[4];
    s32 f480;
    s32 f484;
    char pad488[0x490 - 0x488];
    s32 f490;
    char pad494[0x4A0 - 0x494];
    s32 f4A0;
    s32 f4A4;
    char pad4A8[0x4B8 - 0x4A8];
    s32 f4B8;
    char pad4BC[0x4F0 - 0x4BC];
    s32 f4F0;
    s32 f4F4;
};

extern "C" s32 func_00110498(void *);
extern "C" f32 RaceCourse__getCourseLength(void *);
extern "C" s32 func_003EF330(Run *);
extern "C" char *func_00445450(char *);
extern "C" s32 func_00445AE8(char *, char *);
extern "C" char *func_004472A0(void *);
extern "C" s32 func_00447CA0(long, char *);
extern "C" char *func_005A609C(char *, const char *);

static inline s32 cnt(List *const &l) { return l->count; }
extern "C" void func_00119918(Self *self, Run *run, W *w) {
    Course *course = w->u6C;
    s32 n = cnt(course->p60);
    s32 i;
    for (i = 0; i < n; i++) {
        char *base = (char *)self + 4;
        Car *car = course->p60->p8[i];
        s32 idx = car->f3444;
        func_005A609C((char *)self + idx * 0x80 + 4, func_00445450((char *)car + 0x20));
        func_00445AE8((char *)course->p60->p8[i] + 0x20, (char *)self + idx * 0x10 + 0x304);
        {
            VEntryI *e = (VEntryI *)(run->vtbl + 0x60);
            *(s32 *)(base + idx * 4 + 0x360) = e->fn((char *)run + e->delta, i);
        }
        {
            VEntryI *e = (VEntryI *)(run->vtbl + 0x60);
            Car *c2 = course->p60->p8[i];
            c2->f3460 = e->fn((char *)run + e->delta, i);
        }
    }
    self->f484 = run->f170;
    self->f478 = n;
    self->f4A0 = course->p60->p8[0]->f3444;
    {
        VEntryL *e = (VEntryL *)(run->vtbl + 0x10);
        func_00447CA0(e->fn((char *)run + e->delta), (char *)self + 0x3C8);
    }
    func_005A609C((char *)self + 0x408, func_004472A0(course->p70));
    self->f4A4 = 0;
    s32 r = func_00110498(course->p70);
    self->f490 = r;
    course->f970 = r != 0;
    self->f46C = 0;
    self->f480 = 0;
    self->f4B8 = func_003EF330(run);
    self->f4F4 = course->f0;
    self->f4F0 = (s32)RaceCourse__getCourseLength(course->p80);
}
