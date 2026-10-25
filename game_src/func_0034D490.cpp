/* compiler: ee-gcc2.96-no-strict-aliasing */
typedef unsigned char u8;
typedef short s16;
typedef float f32;
typedef int I128 __attribute__((mode(TI)));

struct VEntry {
    s16 delta;
    s16 index;
    void *fn;
};

struct Course;

struct EntryList {
    char pad0[8];
    struct Entry **entries;
};

struct Owner {
    char pad0[0x60];
    EntryList *list;
    char pad64[0x80 - 0x64];
    Course *course;
};

struct Entry {
    char pad0[0x20];
    char spec[0x3444 - 0x20];
    int start;
    char pad3448[0x3468 - 0x3448];
    char settings[4];
};

struct CarState {
    char pad0[0x32];
    u8 type;
    char pad33[0x10D1 - 0x33];
    u8 x10D1;
};

struct Entrant {
    char pad0[0x10];
    CarState *state;
    char pad14[0x9A0 - 0x14];
    char x9A0[0x10A0 - 0x9A0];
};

struct CarData {
    char data[0x1100];
};

struct Event {
    u8 op;
    u8 b1;
    u8 b2;
    u8 b3;
};

struct ADPSource {
    char pad0[0x23];
    u8 x23;
    u8 x24;
    u8 x25;
};

struct Conductor {
    Owner *owner;
    char pad4[0x98 - 4];
    CarData carData[11];
    char pad[0xCBD8 - 0x98 - 11 * 0x1100];
    Course *course;
    u8 mode;
    char padCBDD[0xCBE3 - 0xCBDD];
    u8 xCBE3;
    u8 xCBE4;
    char padCBE5[1];
    u8 code[8];
    char padCBEE[0xCBF4 - 0xCBEE];
    s16 xCBF4;
    char padCBF6[0xCBFC - 0xCBF6];
    int timer[12];
    char padCC2C[0xCD7C - 0xCC2C];
    char grid[0xDA50 - 0xCD7C];
    char xDA50[0xF858 - 0xDA50];
    u8 xF858;
    char padF859[0xF866 - 0xF859];
    u8 xF866[6];
    s16 xF86C[6];
    s16 delay[6];
    s16 xF884;
    u8 xF886;
    u8 xF887;
    char padF888[0xF8A0 - 0xF888];
    int count;
    char padF8A4[0xF8F8 - 0xF8A4];
    Event evB[6];
    Event evA[6];
    char padF928[0x10140 - 0xF928];
    char *vtbl;
};

struct Vec {
    f32 x;
    f32 y;
    f32 z;
    f32 w;
};

static inline f32 vx(I128 v) {
    f32 f;
    __asm__("mtc1 %1, %0" : "=f"(f) : "r"(v));
    return f;
}

static inline I128 vsr32(I128 v) {
    I128 r;
    __asm__("dsra32 %0, %1, 0" : "=r"(r) : "r"(v));
    return r;
}

static inline I128 vhi(I128 v) {
    I128 r;
    __asm__("pcpyud %0, %1, %1" : "=r"(r) : "r"(v));
    return r;
}

extern char D_0069F648[];

extern "C" {
void func_0034C7E0(Conductor *) throw();
u8 DynamicsConductor__getCourseType(Conductor *) throw();
Entrant *func_0034C190(Conductor *, int) throw();
void func_00343AB8(Entrant *, Conductor *, Owner *, CarData *) throw();
void func_00343ED0(Entrant *) throw();
void func_00344040(Entrant *) throw();
ADPSource *GetADPsource(Conductor *, int, unsigned int);
void func_00414658(char *, Conductor *);
void func_0046B0A8(char *, Course *);
int func_0046B9F0(char *);
void func_00360348(Conductor *);
void func_003F0100(void);
void *func_005A48D8(void *, int, unsigned int);
void func_003F3B68(char *, char *, int, int, int, char *);
I128 func_003952C0(Course *, int);
f32 func_00395228(Course *, int);
int func_00363B10(Entrant *, char *, int, int, int, int, int, int, f32, f32, f32, f32, int, int, char *);
void func_003F2560(Entrant *);
int func_004462A0(char *);
void func_00385410(char *, char *);
void func_0035DCF0(Entrant *);
void func_003F1E60(Conductor *);
void func_003F71A0(Entrant *, int);
}

static inline VEntry *ventry(Conductor *self, int off) {
    return (VEntry *)(self->vtbl + off);
}

extern "C" void DynamicsConductor__initialize(Conductor *self, int mode) {
    int m = (u8)mode;
    Owner *owner = self->owner;
    Course *course = owner->course;
    int i;

    self->mode = mode;
    self->course = course;
    if (m == 1)
        self->mode = 0;
    else if (m == 3)
        self->mode = 2;
    func_0034C7E0(self);
    if (self->code[0]) {
        ADPSource *src = GetADPsource(self, DynamicsConductor__getCourseType(self), 0);
        int d = -(src->x23 * 10);
        self->xF884 = d;
        self->xF886 = src->x24;
        self->xF887 = src->x25;
        if (d == 0)
            self->xF884 = -400;
        if (self->xF886 == 0)
            self->xF886 = 80;
        if (self->xF887 == 0)
            self->xF887 = 20;
        int n = self->xF858 + 1;
        for (i = 0; i < n; i++) {
            if (self->delay[i] || self->xF86C[i] || self->xF866[i])
                self->xF886 = 0;
        }
        {
            VEntry *e = ventry(self, 0x30);
            if (((int (*)(char *))e->fn)((char *)self + e->delta))
                self->xF886 = 0;
        }
    }
    self->xCBE4 = 0;
    self->xCBE3 = 0;
    self->xCBF4 = 0;
    func_00414658(self->grid, self);
    func_0046B0A8(self->xDA50, self->course);
    func_0046B9F0(self->xDA50);
    func_00360348(self);
    func_003F0100();
    {
        VEntry *e = ventry(self, 0xD0);
        ((void (*)(char *, int))e->fn)((char *)self + e->delta, 1);
    }
    for (i = 0; i < self->count; i++) {
        Entry *entry = owner->list->entries[i];
        Entrant *car = func_0034C190(self, i);
        char *spec = entry->spec;
        Vec pos;
        Vec rot;
        int control;
        int arg4;
        int arg5;
        int grid;
        char cfg[0x600];
        int mode2;
        int distance;
        int speed;
        I128 p;
        int start;

        func_005A48D8(&self->evA[i], 0, sizeof(Event));
        func_005A48D8(&self->evB[i], 0, sizeof(Event));
        func_00343AB8(car, self, owner, &self->carData[i]);
        {
            VEntry *e = ventry(self, 0xE8);
            ((void (*)(char *, int, int *, int *, int *, int *))e->fn)((char *)self + e->delta, i, &grid, &arg4,
                                                                       &control, &arg5);
        }
        func_003F3B68(cfg, spec, grid, DynamicsConductor__getCourseType(self), 0, D_0069F648);
        start = entry->start;
        p = func_003952C0(owner->course, start);
        pos.x = vx(p);
        pos.y = -vx(vhi(p));
        pos.z = vx(vsr32(p));
        rot.z = func_00395228(owner->course, start);
        mode2 = 0;
        distance = 0;
        speed = 0;
        {
            VEntry *e = ventry(self, 0xF0);
            ((void (*)(char *, int, int *, int *, int *))e->fn)((char *)self + e->delta, i, &mode2, &distance,
                                                                &speed);
        }
        func_00363B10(car, cfg, -1, control, arg4, arg5, i, mode2, pos.x, pos.y, pos.z, rot.z, distance, speed, entry->settings);
        self->timer[i] = 0;
        if (speed == 0)
            self->timer[i] = self->delay[i] * 60;
        func_003F2560(car);
        {
            CarState *st = car->state;
            st->x10D1 = 0;
            if (func_004462A0(spec))
                st->x10D1 = 1;
            else if (st->type == 2)
                st->x10D1 = 1;
        }
        func_00385410(spec, car->x9A0);
        func_0035DCF0(car);
        func_00343ED0(car);
        func_00344040(car);
    }
    func_003F1E60(self);
    {
        VEntry *e = ventry(self, 0xD8);
        ((void (*)(char *, int))e->fn)((char *)self + e->delta, 1);
    }
    for (i = 0; i < self->count; i++)
        func_003F71A0(func_0034C190(self, i), 0);
}
/* native correction of src/race/DynamicsConductor_DynamicsConductorBattle2P/DynamicsConductor__initialize.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_00363B10 */
