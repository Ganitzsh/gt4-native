/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native stand-in for 0022FFD0 (mEnterEvent__structor_0, the render context's per-frame draw;
 * no matched source). Written from the original instructions, not judged against them:
 *  - resets the clip-state stack (0x1D00 = 0, 0x720 = the screen rect from 002314A8, identity
 *    matrix and alpha 1 in entry 0), counts the frame (0x1D54);
 *  - lets the transition at 0x6D8 begin (vtable 0x1C0), draws every root window of the list at
 *    0x28..0x2C (00231508 push state, vtable 0x2E8 draw, 00231798 pop), then the overlay window
 *    at 0x1D10 clipped to the screen;
 *  - when the focus changed (0x6DC), posts a leave event (vtable 0066E148) to the old focus and an
 *    enter event (vtable 0066ABD8) to the new one;
 *  - moves the cursor window at 0x1D0C towards its target (0x6EC/0x6F8/0x704 eased by 0057B0F8,
 *    0x1D38/0x1D3C stepped by one pixel), gives it a transform rotated about its centre, draws it;
 *  - lets the transition end (vtable 0x1C8).
 * Every local has the size its callee writes: the two event and transform handles are one
 * pointer, getWindowSize writes one float to each pointer, 002A3A18 one int to each. */

typedef int s32;
typedef short s16;

struct VEntry {
    s16 delta;
    s16 index;
    void (*fn)(void *, void *, void *);
};

static inline VEntry *ventry(char *obj, int off) { return (VEntry *)(*(char **)(obj + 4) + off); }
static inline void vcall2(char *obj, int off, void *a) {
    VEntry *e = ventry(obj, off);
    ((void (*)(void *, void *))e->fn)(obj + e->delta, a);
}
static inline void vcall3(char *obj, int off, void *a, void *b) {
    VEntry *e = ventry(obj, off);
    e->fn(obj + e->delta, a, b);
}

#define I32(p, o) (*(s32 *)((char *)(p) + (o)))
#define F32(p, o) (*(float *)((char *)(p) + (o)))
#define PTR(p, o) (*(char **)((char *)(p) + (o)))

extern char D_006984C0[], D_0066E148[], D_0066ABD8[];

extern "C" {
s32 func_0022F0F8(char *);
s32 func_0022F2A0(char *);
void func_0022F2F8(char *, s32);
s32 func_0022F350(char *);
void func_0022F3A8(char *, s32);
void func_00230B68(char *, char *);
void func_00230CF8(char *);
char *func_002314A8(char *);
void func_00231508(char *, char *);
void func_00231798(char *, char *);
void func_00231A50(char *, char *);
void func_00231C48(char *);
void func_00232210(char *);
void func_0024B248(void *, s32);           /* transform handle destructor */
void func_0024BEB0(char *, float, float);   /* translate */
void func_0024BEF8(char *, float);          /* rotate (degrees) */
void func_0024C060(char *);                 /* identity */
s32 func_0024D1B0(char *);
void func_00250CE0(char *, void *);         /* post an event (handle) */
void mWidget__getWindowSize(char *, float *, float *);
void func_0025BA68(char *, float, float);
void func_00265E50(char *, s32);
s32 func_00266590(s32);
void func_002665A0(char *, s32);
void func_00266830(char *, s32);
void func_00266900(char *, s32);
void *func_00267808(void *out, char *widget); /* the widget's transform handle */
void func_00267930(char *, void *);
s32 func_0028B180(void *out, char **event);   /* event handle from an event */
void func_0028D8F0(void *, s32);            /* enter-event handle destructor */
void func_0028E8E8(char *, char *, s32);    /* mEvent::mEvent(this, source, target) */
void func_002A3A18(char *, s32 *, s32 *);
void func_002A3A30(char *, s32, s32);
void func_002ADEE0(void *, s32);            /* leave-event handle destructor */
char *func_00326750(unsigned int, s32, const char *);
void func_0057B0F8(char *, float, float);   /* ease value at +8 towards a target */
}

static inline float absf(float v) { return 0.0f <= v ? v : -v; }

static void post_event(char *self, char *vtable, int enter) {
    char *queue = PTR(self, 0x10);
    s32 target = func_0022F2A0(self);
    char *event = func_00326750(0x20, 4, D_006984C0);
    func_0028E8E8(event, self, target);
    PTR(event, 4) = vtable;
    char *ref = event;
    char *handle[4];
    func_0028B180(handle, &ref);
    func_00250CE0(queue, handle);
    if (enter)
        func_0028D8F0(handle, 2);
    else
        func_002ADEE0(handle, 2);
}

static void draw_window(char *self, char *w) {
    func_00231508(self, w);
    vcall2(w, 0x2E8, self);
    func_00231798(self, w);
}

extern "C" void func_0022FFD0(char *self) {
    char *screen = self + 0x720;
    I32(self, 0x1D00) = 0;
    I32(self, 0x1D54) += 1;
    float *src = (float *)func_002314A8(self);
    if ((float *)screen != src) {
        F32(screen, 0) = src[0];
        F32(screen, 4) = src[1];
        F32(screen, 8) = src[2];
        F32(screen, 0xC) = src[3];
    }
    float *m = (float *)(self + I32(self, 0x1D00) * 0x38 + 0x730);
    m[0] = 1.0f; m[1] = 0.0f; m[2] = 0.0f;
    m[3] = 0.0f; m[4] = 1.0f; m[5] = 0.0f;
    m[6] = 0.0f; m[7] = 0.0f; m[8] = 1.0f;
    F32(self, I32(self, 0x1D00) * 0x38 + 0x754) = 1.0f;

    char **transition = (char **)(self + 0x6D8);
    if (*transition != 0 && func_0024D1B0(*transition) != 0)
        vcall3(*transition, 0x1C0, self, self + 0x24);

    char **it = (char **)PTR(self, 0x28);
    char **end = (char **)PTR(self, 0x2C);
    for (; it != end; ++it) {
        if (I32(self, 0x1D48) != 0 && I32(self, 0x1D40) == 0)
            func_0022F3A8(self, 0);
        draw_window(self, *it);
    }

    if (PTR(self, 0x1D10) != 0) {
        func_00265E50(PTR(self, 0x1D10), 1);
        func_00266830(PTR(self, 0x1D10), 0);
        func_00231A50(self, func_002314A8(self));
        draw_window(self, PTR(self, 0x1D10));
        func_00231C48(self);
        func_00266900(PTR(self, 0x1D10), 0);
        func_00265E50(PTR(self, 0x1D10), 0);
    }

    if (I32(self, 0x6DC) != 0) {
        s32 focus = func_0022F350(self);
        if (focus != func_0022F2A0(self)) {
            if (func_0022F2A0(self) != 0)
                post_event(self, D_0066E148, 0);
            func_0022F2F8(self, func_0022F350(self));
            if (func_0022F2A0(self) != 0)
                post_event(self, D_0066ABD8, 1);
        }
    }

    func_00232210(self);
    if (func_0022F0F8(self) != 0) {
        func_00230B68(self, (char *)func_0022F0F8(self));
        if (I32(self, 0x1D50) != 0)
            func_00230CF8(self);
    }

    if (I32(self, 0x1D08) != 0 && I32(self, 0x1D48) != 0) {
        char *ex = self + 0x6EC;   /* eased x, value at +8 */
        char *ey = self + 0x6F8;   /* eased y */
        char *ea = self + 0x704;   /* eased angle */
        if (I32(self, 0x6E0) == 0 && I32(self, 0x1D50) != 0) {
            if (I32(self, 0x1D40) != 0) {
                float tx = F32(self, 0x1D2C);
                float dx = absf(tx - F32(ex, 8));
                float dy = absf(F32(self, 0x1D30) - F32(ey, 8));
                float speed = F32(PTR(self, 0x10), 0xA0) * 0x1.333332p-3f;
                if (dx < 1.0f)
                    F32(ex, 8) = tx;
                else
                    func_0057B0F8(ex, tx, speed);
                if (dy < 1.0f)
                    F32(ey, 8) = F32(self, 0x1D30);
                else
                    func_0057B0F8(ey, F32(self, 0x1D30), speed);
                if (dx < 1.0f && dy < 1.0f) {
                    float ta = F32(self, 0x1D34);
                    float da = absf(ta - F32(ea, 8));
                    if (da < 1.0f)
                        F32(ea, 8) = ta;
                    else
                        func_0057B0F8(ea, ta, speed);
                    if (da < 1.0f)
                        I32(self, 0x1D40) = 0;
                    I32(self, 0x1D44) = 1;
                }
            }
            if (I32(self, 0x1D44) != 0) {
                s32 px, py;
                func_002A3A18(PTR(self, 0x1D0C), &px, &py);
                s32 ddx = I32(self, 0x1D38) - px;
                s32 ddy = I32(self, 0x1D3C) - py;
                if (ddx == 0 && ddy == 0) {
                    I32(self, 0x1D44) = 0;
                } else {
                    if (ddx < 0) --px;
                    else if (ddx > 0) ++px;
                    if (ddy < 0) --py;
                    else if (ddy > 0) ++py;
                    func_002A3A30(PTR(self, 0x1D0C), px, py);
                }
            }
        }
        if (func_0022F350(self) != 0)
            func_002665A0(PTR(self, 0x1D0C), func_00266590(func_0022F350(self)));
        else
            func_002665A0(PTR(self, 0x1D0C), 0);

        char *xf[4];   /* transform handle: xf[0] is the transform */
        func_00267808(xf, PTR(self, 0x1D0C));
        float w, h;
        mWidget__getWindowSize(PTR(self, 0x1D0C), &w, &h);
        w *= 0.5f;
        h *= 0.5f;
        func_0024C060(xf[0]);
        func_0024BEF8(xf[0], F32(ea, 8));
        func_0024BEB0(xf[0], -w, 0.0f);
        func_00267930(PTR(self, 0x1D0C), xf);
        func_0025BA68(PTR(self, 0x1D0C), F32(ex, 8), F32(ey, 8));
        draw_window(self, PTR(self, 0x1D0C));
        func_0024B248(xf, 2);
    }

    if (*transition != 0 && func_0024D1B0(*transition) != 0)
        vcall2(*transition, 0x1C8, self);
}
