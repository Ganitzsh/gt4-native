/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 00363B10: the lanes of the 128-bit point its callee returns read as floats (the original moves them
   out of a 128-bit register: MTC1, PCPYUD, DSRA32). */
/* Per-car (re)initialisation for the start or restart of a race, called from DynamicsConductor::initialize for entry `index`. In order: choose
 * the grid stage (start 0..2 from the section-set offset when a start distance is given), load default car settings (003433E8) or the given
 * settings block (0035DCA8), copy the entry's body kind from the race entry list, build the driver-assist flag byte at state +0x4E5 from the
 * entry's three assist settings (forced off in some game types and by data checks) and two option bytes of D_00620CD8, run the setup
 * routine 00361908 on the car data (temporarily swapping two data records in game type 2), and reset the physics block. The start position
 * comes from one of three sources chosen by mode: the course query 0034C5E0 (modes 1..5, with `distance` wrapped by the course length into a
 * lap count and an offset, and the lane found through 0046B108), the table at conductor+0xDA50 via 0046BEC0 (modes 6 and 7), or the
 * coordinates given (modes 0 and 8 additionally shift them by the car's length along the heading). Placement is then done by 003630C0, and a
 * long list of state fields (lap +0x4AC, section +0x4AE, flags, penalties, timers such as 120/180/300 for some modes) is reset. A start
 * speed (speed * 0x1.1c71c6p-2, i.e. km/h to m/s) is applied through 00363868 and the result of 00361908 is returned. The meaning of mode
 * values and of most state fields (named by offset) is inferred from this use only; the mode numbers are not named anywhere. */
typedef unsigned char u8;
typedef signed char s8;
typedef int I128 __attribute__((mode(TI)));

struct VEntry { short d, i; int (*f)(char *); };
struct VEntryF { short d, i; float (*f)(char *); };
struct VEntryFI { short d, i; float (*f)(char *, int); };

struct Entry { char p0[0x343C]; int m343C; char p3440[4]; int m3444; char p3448[4]; int m344C; int m3450; int m3454; int m3458; };
struct EntryList { char p0[8]; Entry **e; };
struct Race { char p0[0x60]; EntryList *m60; char p64[0x1C]; char *m80; };
struct Car { float m0; char p4[0x2D]; u8 m31; char p32[0x10D4 - 0x32]; float m10D4; float m10D8; };
struct Con {
    char p0[0xCBDE]; u8 mCBDE;
    char pCBDF[0xDA50 - 0xCBDF]; char mDA50[0xF818 - 0xDA50]; char mF818[0xFB2C - 0xF818];
    char mFB2C[6][0xD4];
    char *m10024[6];
    char p1003C[0x10140 - 0x1003C]; char *vtbl;
};
struct State {
    char p0[0x440]; u8 m440; s8 m441; char p442[0x466 - 0x442]; s8 m466;
    char p467[0x4A4 - 0x467]; float m4A4; char p4A8[4]; short m4AC; u8 m4AE; u8 m4AF;
    u8 m4B0; u8 m4B1; u8 m4B2; char p4B3; u8 m4B4; char p4B5[2]; u8 m4B7; short m4B8; short m4BA; u8 m4BC;
    char p4BD[0x4DC - 0x4BD]; int m4DC; float m4E0; u8 m4E4; u8 m4E5;
    char p4E6[0x526 - 0x4E6]; u8 m526; char p527[0x590 - 0x527]; short m590;
    char p592[0x5C1 - 0x592]; u8 m5C1; u8 m5C2; u8 m5C3; char p5C4[0x5FA - 0x5C4]; u8 m5FA; u8 m5FB;
    char p5FC[0x614 - 0x5FC]; int m614; char p618[0x62C - 0x618];
    u8 m62C; u8 m62D; u8 m62E; u8 m62F; u8 m630; u8 m631; u8 m632; char p633[0x64C - 0x633];
    short m64C; s8 m64E; s8 m64F; char p650[0x65B - 0x650]; u8 m65B;
};
struct Physics { char p0[0x28]; float m28; };
struct Self { Race *race; Con *con; char *m8; int mC; Car *car; Physics physics; char p[0x104 - 0x14 - sizeof(Physics)]; State state; };
struct Data { char p0[4]; char recs[0x1F5 - 4]; u8 m1F5; char p1F6[9]; u8 m1FF; };

extern "C" {
void func_003433E8(char *);
int func_003452F0(Self *);
int func_00345400(Self *);
void func_003455F0(Self *);
void func_003487F0(Self *, int);
float DynamicsConductor__GetCourseLength(Con *);
int DynamicsConductor__GetStartVOffsetBySectionSet(Con *);
int func_0034C470(Con *);
int func_0034C5E0(Con *, int *, float *, float *, float *, float *, int, int);
int func_0034C760(Con *, int);
u8 DynamicsConductor__getCourseType(Con *);
int func_0034C998(Con *);
void func_00350EB0(Self *) throw();
void func_00354AF8(Self *, char *, int) throw();
void func_00355840(Self *) throw();
void func_00355DC8(Self *) throw();
void func_0035C7E0(Self *) throw();
void func_0035D108(Self *, int) throw();
void func_0035DCA8(Self *, char *) throw();
void func_0035E3B8(Self *) throw();
void AutomobileSetCrashPenalty(State *, int) throw();
int func_00361908(Car *, Data *, int, int, char *, char *) throw();
int func_00362DF8(Race *) throw() __attribute__((const));
void func_00362E00(State *, int) throw();
void func_00362E10(State *, int) throw();
void func_00362FA8(Self *) throw();
void func_003630C0(Self *, int, float, float, float, float) throw();
void func_00363528(Self *) throw();
void func_00363560(Self *) throw();
void func_00363868(Self *, float) throw();
void func_003658A0(Self *);
void func_00366388(Self *);
void func_003683C8(Self *);
float func_00395228(char *, int);
I128 func_003952C0(char *, int);
void func_003F1700(Data *);
int func_003F1728(Data *);
void func_003F1748(Data *);
int func_003F1750(Data *);
void func_003F1BD8(Con *, int, int, int);
void func_003F27E8(Self *);
void func_003F3360(Self *);
void func_003F9CF0(State *);
void func_004639E0(Physics *, int);
void func_00463AE0(Self *, int);
void func_00466C20(Physics *, int, int);
void func_00467148(Physics *, int);
void func_004673D8(Physics *, Car *);
void func_00469440(Self *);
void func_00469468(Con *);
void func_00469E58(Physics *);
int func_0046B108(char *, float, float, float);
void func_0046BEC0(char *, int, float *, float *, int *);
float func_0057D1F0(float);
float func_0057D2B8(float) throw();
}
extern u8 D_00620CD8[];
extern int D_00620AD8[];

static inline int vcall(Con *c, int off) { VEntry *v = (VEntry *)(c->vtbl + off); return v->f((char *)c + v->d); }
static inline float vcallf(Con *c, int off) { VEntryF *v = (VEntryF *)(c->vtbl + off); return v->f((char *)c + v->d); }
static inline float vcallfi(Con *c, int off, int n) { VEntryFI *v = (VEntryFI *)(c->vtbl + off); return v->f((char *)c + v->d, n); }
static inline float quad_lane(const I128 &v, int k) { return ((const float *)&v)[k]; }

extern "C" int func_00363B10(Self *self, Data *data, int id, int control, int arg4, int arg5, int index, int mode,
                             float x, float y, float z, float angle, int distance, int speed, char *settings) {
    int point = id;
    float px = x, py = y, pz = z, pa = angle;
    int old[2];
    float pos[4] __attribute__((aligned(16)));
    float rot[4] __attribute__((aligned(16)));
    int result;
    Car *car;
    Con *con;
    Race *race;
    int start;
    State *state = &self->state;
    con = self->con;
    start = 0;
    car = self->car;
    race = self->race;
    if (DynamicsConductor__GetStartVOffsetBySectionSet(con) != 0 && distance > 0) {
        start = 1;
        if (distance >= DynamicsConductor__GetStartVOffsetBySectionSet(con)) start = 2;
    }
    if (settings == 0) func_003433E8((char *)self + 0x6DC);
    else func_0035DCA8(self, settings);
    self->m8 = race->m80;
    self->mC = race->m60->e[index]->m343C;
    int kind = 1;
    switch (race->m60->e[index]->m344C) {
    case 0: break;
    case 1: kind = 2; break;
    case 2: kind = 3; break;
    }
    func_003487F0(self, kind);
    state->m440 = index;
    state->m441 = arg5;
    func_00362E00(state, control);
    Entry *e = race->m60->e[index];
    int assist0 = e->m3450;
    int assist1 = e->m3454;
    int assist2 = e->m3458;
    if (func_0034C998(con) != 0) { assist0 = 0; assist1 = 0; }
    if (assist0 == 0) func_003F1700(data);
    if (assist1 == 0) func_003F1748(data);
    if (func_003F1728(data) != 0) assist0 = 0;
    if (func_003F1750(data) != 0) assist1 = 0;
    u8 *flags = &state->m4E5;
    *flags = 0;
    u8 ctl = state->m466;
    if (ctl < 2 || (s8)ctl == 3) {
        if (assist1) *flags = 1;
        if (assist0) *flags |= 4;
        switch (D_00620CD8[0x131]) {
        case 0: break;
        case 1: if (assist1) *flags |= 2; break;
        case 2: if (assist0) *flags |= 2; break;
        case 3: *flags |= 2; break;
        }
        if (assist2) *flags |= 8;
        switch (D_00620CD8[0x133]) {
        case 0: break;
        case 1: if (assist0) *flags |= 0x10; break;
        case 2: *flags |= 0x10; break;
        }
    }
    int sel = data->m1FF;
    char *recs = data->recs;
    int oldsel = data->m1F5;
    int sel3 = sel + 3;
    int off0 = sel * 0xA8;
    int off1 = sel3 * 0xA8;
    int i0 = off0 + 0x275;
    int i1 = off1 + 0x275;
    u8 *a = (u8 *)(recs + i0);
    u8 *b = (u8 *)(recs + i1);
    old[0] = *a;
    old[1] = *b;
    if (func_0034C998(con) == 2) {
        data->m1F5 = sel;
        *a = ((u8 *)(off0 + (int)recs))[0x27F];
        *b = ((u8 *)(off1 + (int)recs))[0x27F];
    }
    result = func_00361908(car, data, state->m466, 0, con->mF818, settings);
    if (func_0034C998(con) == 2) {
        data->m1F5 = oldsel;
        *a = old[0];
        *b = old[1];
    }
    unsigned n = state->m440;
    con->m10024[n] = con->mFB2C[n];
    Physics *physics = &self->physics;
    func_00469440(self);
    func_00466C20(physics, car->m31, DynamicsConductor__getCourseType(con));
    if (vcall(con, 0x1A0) != 0) func_00469468(con);
    if (state->m466 == 2) func_00467148(physics, car->m31);
    int lap = 0;
    func_004673D8(physics, car);
    func_00362E10(state, arg4);
    state->m4B1 = 0; state->m5C2 = 0; state->m5C1 = 0; state->m4B8 = 0;
    state->m64E = -1; state->m64F = -1; state->m64C = 0;
    state->m62C = 0; state->m62D = 0;
    int length = (int)DynamicsConductor__GetCourseLength(self->con);
    if (distance > 0) {
        lap = 1;
        while (distance >= length) { distance -= length; lap++; }
    } else if (distance < 0) {
        while (distance <= -length) distance += length;
    }
    int invalid = 0;
    int lane = -1;
    if (con->mCBDE != 2) {
        I128 v = func_003952C0(race->m80, race->m60->e[index]->m3444);
        lane = func_0046B108(con->mDA50, quad_lane(v, 0), -quad_lane(v, 2), 0.0f);
    }
    if ((unsigned)(mode - 1) < 5) {
        int r = func_0034C5E0(con, &point, &px, &py, &pz, &pa, distance, con->mCBDE == 1 ? lane : -1);
        if (r == 0) mode = 0;
        else if (r < 0) invalid = 1;
    } else if ((unsigned)(mode - 6) < 2) {
        func_0046BEC0(self->con->mDA50, index, pos, rot, &point);
        px = pos[0]; py = pos[1]; pz = pos[2]; pa = rot[2];
    }
    if (mode == 0 || mode == 8) {
        float off = car->m0;
        px += off * func_0057D2B8(pa);
        py -= off * func_0057D1F0(pa);
    }
    state->m65B = 0;
    if (start) {
        if (mode == 2) state->m65B = 1;
        else state->m65B = start;
    }
    func_003630C0(self, point, px, py, pz, pa);
    func_003455F0(self);
    func_00363560(self);
    func_00362FA8(self);
    func_00363528(self);
    AutomobileSetCrashPenalty(state, 0);
    state->m62E = 0; state->m5C3 = 0; state->m62F = 0; state->m630 = 0; state->m631 = 0;
    func_00354AF8(self, con->mF818, index);
    if (car->m10D4 > 0.0f) state->m4E0 = car->m10D8;
    else state->m4E0 = 0.0f;
    state->m4E4 = 0;
    state->m4DC = 0;
    func_003F3360(self);
    func_00355840(self);
    func_00355DC8(self);
    func_0035D108(self, 0);
    func_003658A0(self);
    state->m4B0 = 0;
    state->m4B4 = 0;
    if (invalid) {
        state->m4AE = 0;
        state->m4AC = 0;
    } else {
        int count = vcall(self->con, 0x150) - 1;
        state->m4AE = count;
        for (int i = 0; i < count; i++) {
            if (state->m4A4 < vcallfi(self->con, 0x158, i)) { state->m4AE = i; break; }
        }
        if (mode == 6) {
            if (DynamicsConductor__GetCourseLength(self->con) * 0.5f < state->m4A4) state->m4B0 = 1;
            state->m4AE = 0;
            state->m4AC = 0;
        } else if (lap > 0) {
            state->m4AC = lap;
        } else if (state->m4AE == count) {
            if (func_0034C470(con) != 0 && state->m4A4 < vcallf(con, 0x138)) {
                state->m4AE = 0;
                state->m4AC = 1;
            } else if (!(state->m4A4 < DynamicsConductor__GetCourseLength(con) * 0.5f)) {
                state->m4AE = 0;
                state->m4AC = 0;
            } else {
                state->m4AE = 0;
                state->m4AC = 1;
            }
        } else {
            state->m4AC = 1;
        }
    }
    state->m614 = 0;
    int chosen = -1;
    func_0035E3B8(self);
    state->m4BC = 0; state->m4BA = 0; state->m4B2 = 0; state->m632 = 0; state->m5FA = 0; state->m5FB = 0;
    state->m4B7 = func_0034C760(con, index);
    state->m4AF = index + 1;
    if ((unsigned)(mode - 2) < 4) {
        chosen = 6;
    } else if (mode != 8) {
        if (con->mCBDE == 1) chosen = lane;
        else if (state->m466 == 0) chosen = 7;
        else if (func_00362DF8(race)) {
            int rem = race->m60->e[index]->m3444 % 6;
            I128 v = func_003952C0(race->m80, 0);
            I128 w = func_003952C0(race->m80, 1);
            float x0 = quad_lane(v, 0);
            float z0 = quad_lane(v, 2);
            float dy = -(z0 - quad_lane(w, 2));
            float dx = x0 - quad_lane(w, 0);
            float heading = func_00395228(race->m80, 0);
            float s = -func_0057D2B8(heading);
            float side = dx * func_0057D1F0(heading) - s * dy;
            int left = side < 0.0f ? 1 : 0;
            chosen = D_00620AD8[rem];
            if (!left) {
                if (chosen == 2) chosen = 1;
                else if (chosen == 1) chosen = 2;
            }
        }
    }
    func_003F1BD8(self->con, 2, index, 0);
    func_004639E0(physics, chosen);
    func_00469E58(physics);
    if ((unsigned)(mode - 6) < 2) {
        func_00345400(self);
        state->m4BA = 120;
    }
    state->m590 = 0;
    if ((unsigned)(mode - 2) < 3 || mode == 8) {
        func_003452F0(self);
        state->m526 = 12;
        if (mode == 3) state->m590 = 180;
        else if (mode == 8) state->m590 = 300;
    }
    float velocity;
    if (mode == 4) velocity = physics->m28;
    else velocity = (float)speed * 0x1.1c71c6p-2f;
    if (velocity != 0.0f) func_00363868(self, velocity);
    func_00463AE0(self, mode == 0);
    func_003F9CF0(state);
    func_003F27E8(self);
    func_0035C7E0(self);
    func_003683C8(self);
    func_00350EB0(self);
    func_00366388(self);
    return result;
}
