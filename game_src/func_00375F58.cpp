struct S {
    int a; float f;
};
static inline void init(S *p, int index, float z) { p->a = index; p->f = z; }
struct Record { int flags; int pad4; int value; char padC[0x1C]; };
static inline int flag(const Record *r,int bit) { return (r->flags >> bit) & 1; }
struct Node { char pad[0xB0]; int value; };
struct Owner { int pad; Node *node; };
struct Context { char pad[0x80]; Owner *owner; };
struct VEntry { short delta, index; void *fn; };
struct Target { VEntry *vt; };
struct State { char pad[0x12C]; Target *current; char pad130[0x30]; Target *target; };
extern "C" int func_00375E80(S *, char *, int) throw();
extern "C" int func_003FF3E0(Context *, void *, Record *, int, float);
extern "C" float func_00578598(void *);
extern float D_006210A8[];
extern "C" int func_00375F58(char *self, int index, Context *ctx, void *track, float ratio, int arg4, void *random, int blocked) {
    Record records[16];
    S sorted[16];
    int i;
    int count = 0;
    int invalid = -1;
    S *p = sorted;
    for (i = 15; i >= 0; i--) { init(p, invalid, 0.0f); ++p; }
    int n = func_003FF3E0(ctx, track, records, 16, ratio);
    if (n <= 0) return 0;
    for (i = 0; i < n && count < 16; i++) {
        int flags=records[i].flags;
        int f4=flags>>4;
        flags >>= 3;
        if ((flags&1) && (f4&1)) func_00375E80(sorted, (char *)records, i);
    }
    for (i = 0; i < 16 && sorted[i].a >= 0; i++) count++;
    if (count == 0) {
        for (i = 0; i < n && count < 16; i++) func_00375E80(sorted, (char *)records, i);
        for (i = 0; i < 16 && sorted[i].a >= 0; i++) count++;
    }
    if (blocked != 0) return 0;
    if ((unsigned)(count - 1) >= 16) return 0;
    float f = func_00578598(random);
    if (f <= 0.0f) f = 0.0f;
    if (f >= 1.0f) f = 1.0f;
    int choice = 0;
    while (D_006210A8[choice] < f) choice++;
    if (choice >= count) choice = 0;
    State *state = (State *)(self + index * 0x19C + 0x10);
    Owner *owner = ctx->owner;
    Target *target = state->target;
    VEntry *e = target->vt + 0x27;
    ((int (*)(void *, void *, Owner *, int, int, int, float))e->fn)((char *)target + e->delta, track, owner, owner->node->value, records[sorted[choice].a].value, arg4, ratio);
    state->current = target;
    return 1;
}
/* native correction of src/race/unit_00374E90/func_00375F58.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_003FF3E0 */
