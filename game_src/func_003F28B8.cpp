/* compiler: ee-gcc2.96-nsa-nogcse */
/* Native code for 003F28B8 (unmatched; from the decomp's draft, corrected against the original): mode 6
 * runs a three-state machine on the state byte at +0x592: state 0 goes to 2 when speed >= 2.7778 and
 * throttle >= 0.5, else to 1 when speed >= high (+0x107C); state 1 goes to 2 on the same condition, else
 * to 0 when speed < low (+0x1078); state 2 is unchanged here.
 * The draft left state 0 empty (`case 0: break;`) and put state 0's transitions into case 1, so state 0
 * never left 0. The original's state 0 code is at 0x3F2BF8..0x3F2C30 (c.le.s upper,speed / c.le.s 0.5,
 * throttle -> sb 2; c.le.s high,speed -> sb 1) and state 1's at 0x3F2C38..0x3F2CC0 (-> sb 2, or
 * c.lt.s speed,low -> sb 0). */
/* Updates the assist outputs at +0x594 from the mode byte at owner+0x1077: per mode a small state
 * machine on the speed-like value at +0x53C (hysteresis between the owner thresholds at +0x1078 and
 * +0x107C, plus the throttle-like value at +0x48C in modes 4-6 and a lateral state from +0x554 in
 * mode 4) gives the goal of each output, and each output then moves toward its goal at the owner
 * rate (+0x1080, doubled for the second output) times the step at +0x54C. Meaning of the modes is
 * inferred from func_003F27E8. */
typedef unsigned char u8;
typedef float f32;

struct Data {
    char pad[0x1077];
    u8 mode;
    f32 low;
    f32 high;
    f32 rate;
};

struct Lat { u8 v; };

struct Sub {
    char pad0[0x48C];
    f32 throttle;
    char pad490[0x53C - 0x490];
    f32 speed;
    char pad540[0x554 - 0x540];
    f32 lateral;
    char pad558[0x592 - 0x558];
    u8 state;
    Lat latState;
    f32 out[2];
};

struct Self {
    char pad0[0x10];
    Data *data;
    char pad14[0x54C - 0x14];
    f32 step;
};

extern "C" f32 func_003F2798(Self *, f32) throw();
extern "C" void *func_005A48D8(void *, int, unsigned int);

static inline int simple(Sub *s, f32 high, f32 low) {
    int state = s->state;
    if (state == 0) {
        if (s->speed >= high) {
            s->state = 1;
            state = 1;
        }
    } else if (s->speed < low) {
        s->state = 0;
        state = 0;
    }
    return state;
}

static inline int throttle(Sub *s, f32 high, f32 low) {
    int state = s->state;
    if (state == 0) {
        if (s->speed >= high && s->throttle >= 0.5f) {
            s->state = 1;
            state = 1;
        }
    } else if (s->speed < low || s->throttle < 0x1.999998p-4f) {
        s->state = 0;
        state = 0;
    }
    return state;
}

static inline u8 lateral(Sub *s) {
    Lat state = s->latState;
    f32 g = s->lateral / 0x1.399998p+3f;
    if (state.v == 0) {
        if (g >= 0.5f) {
            s->latState.v = 1;
            state.v = 1;
        } else if (g <= -0.5f) {
            s->latState.v = 2;
            state.v = 2;
        }
    } else if (state.v == 1) {
        if (g < 0x1.999998p-3f) {
            s->latState.v = 0;
            state.v = 0;
        }
    } else if (g > -0x1.999998p-3f) {
        s->latState.v = 0;
        state.v = 0;
    }
    return state.v;
}

static inline __typeof__(lateral((*(Sub**)0))) get88(Sub *s) { return lateral(s); }
extern "C" void func_003F28B8(Self *self) {
    Data *data = self->data;
    int mode = data->mode;
    if (mode == 0)
        return;
    f32 goal[2];
    func_005A48D8(goal, 0, sizeof(goal));
    Sub *s = (Sub *)((char *)self + 0x104);
    u8 n = 1;
    switch (mode) {
    case 1:
        if (simple(s, data->high, data->low) == 1)
            goal[0] = 1.0f;
        else
            goal[0] = 0.0f;
        break;
    case 2:
        goal[0] = func_003F2798(self, s->speed);
        break;
    case 4: {
        n = 2;
        if (throttle(s, data->high, data->low) == 1)
            goal[0] = 1.0f;
        u8 lat = get88(s);
        if (lat == 1)
            goal[1] = 1.0f;
        else if (lat == 2)
            goal[1] = -1.0f;
        break;
    }
    case 5:
        if (throttle(s, data->high, data->low) == 1)
            goal[0] = 1.0f;
        break;
    case 6: {
        f32 high = data->high;
        f32 low = data->low;
        f32 upper = 0x1.638e36p+1f;
        switch (s->state) {
        case 0:
            if (s->speed >= upper && s->throttle >= 0.5f)
                s->state = 2;
            else if (s->speed >= high)
                s->state = 1;
            break;
        case 1:
            if (s->speed >= upper && s->throttle >= 0.5f)
                s->state = 2;
            else if (s->speed < low)
                s->state = 0;
            break;
        case 2:
            if (s->throttle < 0x1.999998p-4f) {
                if (s->speed >= low)
                    s->state = 1;
                else
                    s->state = 0;
            } else if (s->speed < 0x1.1c71c6p-1f)
                s->state = 0;
            break;
        }
        if (s->state == 2)
            goal[0] = 1.0f;
        else if (s->state == 1)
            goal[0] = 0x1.3b13b0p-3f;
        else
            goal[0] = 0.0f;
        break;
    }
    }
    for (int i = 0; i < n; i++) {
        f32 up = self->data->rate;
        f32 down = up;
        if (i == 1) {
            up *= 2.0f;
            down *= 2.0f;
        }
        f32 cur = s->out[i];
        f32 g = goal[i];
        if (cur < g) {
            s->out[i] = cur + up * self->step;
            if (s->out[i] > g)
                s->out[i] = g;
        } else if (cur > g) {
            s->out[i] = cur - down * self->step;
            if (s->out[i] < g)
                s->out[i] = g;
        }
    }
}
