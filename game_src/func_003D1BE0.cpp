/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native correction of the provisional draft drafts/003d1be0/003d1be0_98c6342c27862b82.cpp (argument order, argcheck.py orders): func_00455000 is (float, int) and func_00428D48 (six ints, then two floats) as defined; the draft's orders are the same EE calls but other stack slots natively. */
typedef float f32;
typedef int s32;

struct Vec { f32 x, y, z, w; };
struct Mat { f32 m[4][4]; };
struct Elem { f32 x, y, z; };
struct Ctx {
    Elem rows[4][4]; f32 a[4]; f32 b[4]; s32 valid[4];
    Ctx() { for (int k = 0; k < 4; k++) { valid[k] = 0; a[0] = 0.0f; b[0] = 0.0f; } }
};
struct Player { char data[0x10]; };
struct Channel { f32 f0; f32 x, y, z; f32 angle; char pad14[8]; char sub[0x170 - 0x1C]; s32 f170, f174; };
struct Group { Channel ch[6]; };
struct Sub { char pad[0x540]; s32 f540; char pad544[0xA8C - 0x544]; char data[0xC]; s32 fA98; };
struct Man { s32 id; Sub sub; s32 fAA0; };
struct Pair { f32 v, w; };
struct Cfg { s32 flags[2]; char pad8[8]; f32 f10; char pad14[0x10]; char sub24[0xC]; f32 f30; char pad34[0xA0 - 0x34]; };
struct Tiny { s32 dummy; };
struct Pitmen {
    char pad0[4];
    Tiny tiny;                      /* 0x4 */
    char pad8[0x3C - 0x8];
    s32 done[6];                    /* 0x3C */
    char pad54[0xFC - 0x54];
    s32 kinds[6];                   /* 0xFC */
    char pad114[0x3E4 - 0x114];
    Group groups[6];                /* 0x3E4 */
    Man men[6];                     /* 0x38C4 */
    Cfg cfg[6];                     /* 0x789C */
    char pad7C5C[0x8F30 - 0x7C5C];
    f32 heading[6];                 /* 0x8F30 */
    char pad8F48[0x90F8 - 0x8F48];
    f32 posf[6][3];                 /* 0x90F8 */
    f32 rows[6][4];                 /* 0x9140 */
    char pad91A0[0x9674 - 0x91A0];
    s32 cur;                        /* 0x9674 */
    s32 cur2;                       /* 0x9678 */
    char pad967C[0x9684 - 0x967C];
    s32 modes[6];                   /* 0x9684 */
    char pad969C[0x96D8 - 0x969C];
    Pair a[6][4];                   /* 0x96D8 */
    Pair b[6][4];                   /* 0x9798 */
};
struct Glob { s32 a; s32 b; };
struct Model { char pad[0x7C]; char *p7C; };

extern Glob D_00845680;

extern "C" {
s32 func_0034C190(void *, s32);
void *func_00398758(void *, Vec *);
void func_003C9F00(void *, s32) throw();
void *func_003C9F18(s32) throw();
const char *func_003C9FC8(s32) throw();
f32 func_003CA038(s32) throw() __attribute__((const));
void func_003CA0C8(s32, f32 *, f32 *, f32 *) throw();
void func_003CA668(Ctx *, s32, s32, f32, f32, f32) throw();
void func_003CA698(Ctx *, void *, f32, f32) throw();
void func_003CAAC8(Ctx *) throw();
void func_003CC498(Tiny *, s32, void *, s32) throw();
s32 func_003CC808(void *) throw();
s32 func_003CC840(void *) throw();
Model *func_003CC8E8(void *) throw();
void *func_003CC920(void *) throw();
s32 func_003CC958(s32) throw() __attribute__((const));
s32 func_003CF5C8(void *, s32, s32, s32, s32) throw();
s32 func_003D0B70(f32, f32, f32) throw();
void func_003D0BD0(void *, s32, s32, void *) throw();
void func_003D26C0(void *, s32);
void func_0040A1D0(void *, void *);
void func_00427820(Player *);
void func_00427830(Player *, Model *, s32);
void func_00428D48(Player *, s32, void *, s32, void *, s32, f32, f32);
f32 func_00455000(f32, void *);
void func_00455610(Model *, const char *, s32);
void func_004567E0(s32);
void func_0048D280(Vec *, Mat *, Vec *);
void func_0049AB58(f32);
void func_004A29A8(s32);
void func_004A5348(s32);
void func_004A53F8(void);
void func_004A5400(void);
void func_004A7454(void);
void func_004A7550(f32 *);
void func_004A7698(void *);
void func_004A7844(f32, f32, f32);
void func_004A7890(f32, f32, f32);
void func_004A79B0(f32);
}

static inline void translate(const Elem *v) { f32 x = v->x, y = v->y, z = v->z; func_004A7844(x, y, z); }
static inline f32 sqrtf_(f32 v) { f32 r; __asm__("sqrt.s %0,%1" : "=f"(r) : "f"(v)); return r; }

extern "C" void func_003D1BE0(Pitmen *self, s32 team, void *context, void *camera)
{
    s32 i;
    Glob *g = &D_00845680;

    if (func_003CC8E8(self) == 0) return;
    s32 lod = *(unsigned short *)((char *)func_003CC8E8(self) + 0x2E);
    if (lod >= 21) return;

    s32 changed = team != self->cur;
    self->cur = team;
    self->cur2 = team;
    func_003CC498(&self->tiny, func_003CC840(self), context, team);
    s32 car = func_0034C190(context, team);
    s32 mode = self->modes[team];
    Ctx ctx;
    f32 m[16];
    Player player;
    Vec pos;
    f32 temp[16];
    f32 cp[4];
    f32 result[4];
    f32 shear[16];
    f32 degrees = 180.0f;
    f32 pi = 3.1415f;
    f32 rate = 6.24f;
    f32 radius = 1.5f;
    char *mb = (char *)self;
    for (i = 0; i < 6; i++, mb += 0xAA4) {
        Man *man = (Man *)(mb + 0x38C4);
        s32 motion = func_003CF5C8(self, i, car, mode, team);
        s32 kind = self->kinds[i];
        Cfg *cfg = &self->cfg[team];
        Channel *st = &self->groups[team].ch[i];
        if (motion < 0 || kind == 2) continue;
        s32 busy = st->f170 != 0 || st->f174 != 0;
        if (busy) continue;
        s32 shown = 1;
        func_004A5348(0);
        func_004A53F8();
        func_004A7454();
        func_004A7844(self->posf[team][0], self->posf[team][1], self->posf[team][2]);
        func_004A79B0(self->heading[team] * degrees / pi);
        func_004A7844(st->x, st->y, st->z);
        func_004A79B0((st->angle + func_003CA038(kind)) * degrees / pi);
        func_004A7890(0.94f, 0.94f, 0.94f);
        func_004A7550(m);
        func_004A5400();
        if (!func_003D0B70(m[12], m[13], m[14])) continue;
        func_003C9F00(&self->men[i].sub, i);
        Sub *cur = &self->men[i].sub;
        s32 normal = kind < 3;
        if (!normal) {
            man->fAA0 = shown;
        } else if (man->fAA0 != 0 || changed) {
            man->fAA0 = 0;
            func_004A53F8();
            func_004A7454();
            func_00427820(&player);
            Model *model = func_003CC8E8(self);
            func_00427830(&player, model, func_003CC808(self));
            f32 offset;
            if (func_003CC958(i))
                offset = self->rows[team][i];
            else
                offset = 0.0f;
            s32 anim;
            if (func_003CC958(i) && cfg->flags[i & 1]) {
                if (i < 2)
                    anim = (self->men + i + 2)->id;
                else
                    anim = (self->men + i - 2)->id;
            } else {
                anim = man->id;
            }
            func_00428D48(&player, anim, (void *)func_003C9F18, cur->f540, cur->data, 0, st->f0, offset);
            func_004A5400();
        }
        func_0040A1D0(st->sub, camera);
        if (!normal) {
            const char *name = func_003C9FC8(kind);
            if (name) {
                func_004A53F8();
                func_004A7698(m);
                func_00455610(func_003CC8E8(self), name, 0);
                func_004A5400();
                if (func_003CC958(i)) {
                    func_003CA0C8(kind, &pos.x, &pos.y, &pos.z);
                    func_004A53F8();
                    func_004A7454();
                    func_004A7698(m);
                    func_004A7844(pos.x, pos.y, pos.z);
                    func_004A7550(temp);
                    ctx.valid[i] = 1;
                    func_003CA668(&ctx, i, 3, temp[12], temp[13], temp[14]);
                    func_004A5400();
                }
            }
        } else {
            func_004A53F8();
            f32 a0 = m[0], a1 = m[1], a2 = m[2], a4 = m[4], a5 = m[5], a6 = m[6], a8 = m[8], a9 = m[9], a10 = m[10];
            f32 *t = temp;
            t[0] = a0; t[1] = a4; t[5] = a5; t[9] = a6; t[2] = a8; t[6] = a9; t[4] = a1; t[8] = a2; t[10] = a10;
            t[14] = t[13] = t[12] = 0.0f;
            t[11] = t[7] = t[3] = 0.0f;
            t[15] = 1.0f;
            func_00398758(camera, (Vec *)cp);
            f32 *px = &result[0];
            cp[3] = 0.0f;
            func_0048D280((Vec *)px, (Mat *)t, (Vec *)cp);
            f32 *pz = &result[2];
            f32 len = *px * *px + *pz * *pz;
            if (len >= radius * radius) {
                f32 inv = 1.0f / sqrtf_(len);
                *px = *px * inv * radius;
                *pz = *pz * inv * radius;
            }
            f32 x = *px, z = *pz;
            shear[0] = 1.0f; shear[4] = x; shear[6] = z;
            shear[12] = shear[8] = 0.0f; shear[13] = shear[9] = shear[5] = shear[1] = 0.0f;
            shear[2] = 0.0f; shear[10] = 1.0f; shear[14] = 0.0f; shear[11] = shear[7] = shear[3] = 0.0f;
            shear[15] = 1.0f;
            func_004A7698(m);
            func_004A53F8();
            func_004A5348(1);
            func_004A53F8();
            func_0049AB58(0.01f);
            func_004A5348(0);
            func_004A7698(shear);
            func_004A29A8(0);
            func_003D0BD0(func_003CC920(self), 0, cur->f540, cur->data);
            func_004A29A8(1);
            func_004A5348(1);
            func_004A5400();
            func_004A5348(0);
            func_004A5400();
            if (kind >= 0)
                g->b = kind;
            else
                g->b = func_00455000(1.1f, cur->data) / 420.0f < 0.5f;
            *(Glob **)(func_003CC8E8(self)->p7C + 0x14) = g;
            func_004567E0(0);
            func_003D0BD0(func_003CC8E8(self), cur->fA98, cur->f540, cur->data);
            func_004567E0(1);
            func_004A5400();
            if (func_003CC958(i)) {
                void *tm = func_003C9F18(cur->fA98);
                if (tm) {
                    func_004A53F8();
                    func_004A7454();
                    func_004A7698(m);
                    func_004A7698(tm);
                    func_004A7550(t);
                    ctx.valid[i] = 1;
                    func_003CA668(&ctx, i, 3, temp[12], temp[13], temp[14]);
                    func_004A5400();
                }
            }
        }
        if (func_003CC958(i)) {
            ctx.a[i] = self->a[team][i].v * rate;
            ctx.b[i] = self->b[team][i].v * rate;
        }
    }
    char *cf = (char *)self + team * 160;
    func_003CA698(&ctx, cf + 0x78C0, *(f32 *)(cf + 0x78CC), *(f32 *)(cf + 0x78AC));
    func_003CAAC8(&ctx);
    if (self->done[team] == 0) func_003D26C0(self, car);
}
