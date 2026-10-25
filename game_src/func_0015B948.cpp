/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Script method insertEntryMaxSpeed: with at least 4 script arguments it opens the course-record list of the unit (func_0015AAC0), reads the max-speed value from argument 0 (virtual slot +0x60, a float), the car/entry name from argument 1 (virtual slot +0x18, a string) and calls arguments 2 and 3 for their integer values (results unused here, as in the source), then builds a 0x28-byte request with the name and fixed flags, packs it into a 0x20-byte record (func_0044B258) and stores it with func_00433348 into the list. Sibling of insertEntry (func_00433260, integer key). Which slot is which game value is inferred from the method name only; the request layout is not determined. Note the name pointer is used after the temporary string was released, as in the original. */
typedef int s32;
typedef short s16;

struct Rep {
    s32 len;
    s32 cap;
    s32 ref;
    s32 sel;
};

struct S00659988 {
    const char *name;
};

struct Str {
    char *p;
    char pad[0xC];
};

struct Handle {
    char pad0[0x10];
    s32 p10;
};

struct B32 { char x[0x20]; };
struct In { s32 f0, f4, f8, fC, f10, f14, f18, f1C, f20, f24; const char *f28; };

struct VEntry58 {
    s16 delta;
    s16 index;
    s32 (*fn)(void *);
};

struct VEntry60 {
    s16 delta;
    s16 index;
    float (*fn)(void *);
};

struct VEntry18 {
    s16 delta;
    s16 index;
    Str (*fn)(void *);
};

struct VObj {
    char pad[4];
    char *vtbl;
};

struct Hold {
    Handle *p;
    char pad[0xC];
};

extern char D_0068FFA0[];

extern "C" void func_0015AA68(void *arg0, int arg1) throw();
extern "C" void func_0015AAC0(void *arg0, void *arg1) throw();
extern "C" void func_00326798(void *p, s32 size, s32 align, const char *name);
extern "C" s32 func_00433348(s32, const char *, B32 *, float);
extern "C" s32 func_00447BB8(const char *arg0);
extern "C" s32 func_004482C0(s32 a);
extern "C" void func_0044B258(In *src, B32 *dst, s32 n);
extern "C" void func_005A48D8(void *p, s32 c, s32 n);
extern "C" struct S00659988 *func_005C11A8(void);

static inline float vcall60(VObj *o) {
    VEntry60 *e = (VEntry60 *)(o->vtbl + 0x60);
    return e->fn((char *)o + e->delta);
}

static inline s32 vcall58(VObj *o) {
    VEntry58 *e = (VEntry58 *)(o->vtbl + 0x58);
    return e->fn((char *)o + e->delta);
}

static inline Rep *str_rep(Str *s) { return (Rep *)s->p - 1; }
static inline char *rep_data(Rep *r) { return (char *)(r + 1); }
static inline s32 str_length(Str *s) { return str_rep(s)->len; }
static inline void str_terminate(Str *s) { rep_data(str_rep(s))[str_length(s)] = 0; }
static inline const char *c_str(Str *s) {
    if (str_length(s) == 0) return D_0068FFA0;
    str_terminate(s);
    return rep_data(str_rep(s));
}
static inline void release(Str *s) {
    Rep *q = str_rep(s);
    if (--q->ref == 0) {
        s32 size = q->cap + 0x10;
        func_00326798(q, size, 4, func_005C11A8()->name);
    }
}

extern "C" void MCourseRecordUnit__insertEntryMaxSpeed(void *arg0, void *recordHandle, s32 argCount, VObj **args) {
    Hold listHandle;
    if (argCount >= 4) {
        float maxSpeed;
        const char *name;
        func_0015AAC0(&listHandle, recordHandle);
        maxSpeed = vcall60(args[0]);
        VObj *argObject = args[1];
        VEntry18 *nameSlot = (VEntry18 *)(argObject->vtbl + 0x18);
        Str nameStr = nameSlot->fn((char *)argObject + nameSlot->delta);
        Str *nameStrPtr = &nameStr;
        In request;
        B32 packed;
        name = c_str(&nameStr);
        release(&nameStr);
        vcall58(args[2]);
        vcall58(args[3]);
        request.f4 = func_004482C0(func_00447BB8(name));
        request.f0 = 0;
        request.f8 = 1;
        request.f10 = 1;
        request.f14 = 1;
        request.f18 = 1;
        request.f1C = 0;
        request.f20 = 1;
        request.f24 = 1;
        request.f28 = name;
        func_005A48D8(&packed, 0, 0x20);
        func_0044B258(&request, &packed, 0x20);
        func_00433348(listHandle.p->p10, name, &packed, maxSpeed);
        func_0015AA68(&listHandle, 2);
    }
}
/* native correction of src/gamedata/unit_0015AD00/MCourseRecordUnit__insertEntryMaxSpeed.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_00433348 */
