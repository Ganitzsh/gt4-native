/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native code for 004684A8 (unmatched; from the decomp's draft, corrected against the original): maps a
 * value (f) between two limits (by mode) to 0..1 and feeds it to 00468478, then adds 0x1000 (or a third and
 * two thirds) to the 16-bit histogram entries of the current slot at +0x8C + 0x30 / 0x38.
 * The draft called 00468478 (int a, float f, int b) with two arguments: the original leaves its own mode in
 * $a1 for b (jal at 0x468548 sets only $a0 and $f12), which the EE's separate float registers allow.
 * Natively b came from the stack slot after the float, garbage: an index of 0x3DCCCCD0 crashed B-spec
 * races in 004682A0. */
typedef unsigned short u16;
struct VEntry { short delta, index; int (*fn)(void *); };
struct Obj { char pad[4]; char *owner; char pad8[0x54]; signed char flags[8]; };
extern "C" int func_00468458(Obj *) throw();
extern "C" void func_00468478(Obj *, float, int) throw();
static inline float scale(float f, float lo, float hi) {
    return (f-lo)*(1.0f/(hi-lo));
}
static inline void add(u16 *p, int n) {
    n = *p + n;
    if (n > 65535) n = 65535;
    *p = n;
}
extern "C" void func_004684A8(Obj *self, int mode, float f) {
    float lo, hi;
    if (mode == 0) { lo = 0x1.0aaaa8p+4f; hi = 0x1.e8e38cp+5f; }
    else { lo = 0x1.0aaaa8p+4f; hi = 0x1.20e38cp+6f; }
    float v = (f - lo) * (1.0f / (hi-lo));
    if (v < 0.0f) v = 0.0f; else if (v > 1.0f) v = 1.0f;
    int active = 0;
    func_00468478(self, v, mode);
    char *owner = self->owner;
    VEntry *e = (VEntry *)(*(char **)(owner + 0x10140) + 0x30);
    if (e->fn(owner + e->delta) == 0 && *(int *)(owner + 0xF8A0) >= 2) {
        for (unsigned i = 0; i < 8; i++) {
            if (self->flags[i]) { active = 1; break; }
        }
    }
    char *base = (char *)self + 0x8C;
    int idx = func_00468458(self) / 2;
    int amount = 0x1000;
    if (!active) {
        add((u16 *)(base + (unsigned)(idx * 2 + 0x30)), amount);
    } else {
        add((u16 *)(base + (unsigned)(idx * 2 + 0x30)), amount / 3);
        add((u16 *)(base + (unsigned)(idx * 2 + 0x38)), (amount * 2) / 3);
    }
}
