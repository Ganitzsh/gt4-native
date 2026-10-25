/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native correction of the provisional draft drafts/003e2b28/003e2b28_8334d2697889a500.cpp (argument order, argcheck.py orders): func_0057D8A0 (sincos) takes (float *sine, float *cosine, float x); the draft declared the float first, which on the EE is the same call ($f12, $a0, $a1) but natively put x in the pointers' stack slots. */
struct Vec3 {
    float x, y, z;
};

struct Shape {
    unsigned char b0;
    unsigned char b1;
    unsigned char flags;
    signed char npts;
    float Ix, Iy, Iz;
    char pad10[0x84];
    float mass;
    char pad98[8];
};

struct ShapeTable {
    char pad[0x20];
    Shape shapes[1];
};

struct RigidBody {
    int f0;
    float rx, ry, rz;
    Vec3 pos;
    Vec3 vel;
    float wx, wy, wz;
    float plane[4];
    unsigned char shape;
    unsigned char timer;
    unsigned char b46, b47;
    int runway;
    Vec3 *force;
    Vec3 *torque;
    int f54;
    unsigned char state[8];
};

struct HitCtx {
    Vec3 *from;
    RigidBody *body;
    Vec3 *pts;
    int hit;
};

extern ShapeTable *D_006D6054;
extern void *D_006D6060;
extern int D_00621D08;
extern float D_008456B0[4];
extern const float D_006A3960[3];

extern "C" int func_003E2100(int) throw() __attribute__((const));
extern "C" unsigned int func_003E29F8(RigidBody *) throw();
extern "C" void func_003E26D0(int, HitCtx *);
extern "C" void func_00395A38(void *, void *, void *, int, float, float, float, float);
extern "C" void func_003E3290(RigidBody *);
extern "C" void func_003E3458(RigidBody *, Vec3 *);
extern "C" void func_003E3748(RigidBody *, Vec3 *, int);
extern "C" void func_003E3C48(RigidBody *, Vec3 *, int);
extern "C" void func_0057D8A0(float *, float *, float);
extern "C" float func_005FE3A0(RigidBody *);

// Store two equal stage weights at an arbitrary stride.
static inline void weightPair(float *first, int stride, float value)
{
    first[0] = value;
    first[stride] = value;
}
static inline void initializeDrift(float *weights, int *initialized)
{
    weightPair(weights + 2, -1, -0x1.67a2d8p-3f);
    weightPair(weights + 3, -3, 0x1.59e8b6p-1f);
    *initialized = 1;
}

/* RigidBody::Update */
extern "C" int func_003E2B28(RigidBody *self, float dt)
{
    Vec3 pts[8];
    int i;

    self->f54 = 0;
    if (self->timer) {
        self->timer++;
        if (func_003E2100(self->timer))
            return 0;
    }
    Shape *sh = &D_006D6054->shapes[self->shape];
    unsigned int flags = sh->flags;
    int n = sh->npts;
    if ((flags >> 1) & 1)
        return 1;
    Vec3 force;
    force.x = 0.0f;
    force.y = -sh->mass * 0x1.39eb84p+3f;
    force.z = 0.0f;
    Vec3 torque;
    torque.x = 0.0f;
    torque.y = 0.0f;
    torque.z = 0.0f;
    if (self->force) {
        force.x += self->force->x;
        force.y += self->force->y;
        force.z += self->force->z;
    }
    if (self->torque) {
        torque.x += self->torque->x;
        torque.y += self->torque->y;
        torque.z += self->torque->z;
    }
    Vec3 old;
    old.x = self->pos.x;
    old.y = self->pos.y;
    old.z = self->pos.z;
    func_005FE3A0(self);

    int count = 0;
    if (!self->force && !self->torque) {
        for (i = 0; i < n; i++)
            if (self->state[i] == 2)
                count++;
    }
    int rest = count >= 3;

    if (!rest) {
        float sx, cx, sz, cz;
        float wx = self->wx, wy = self->wy, wz = self->wz;
        float Ix = sh->Ix, Iy = sh->Iy, Iz = sh->Iz;
        func_0057D8A0(&sx, &cx, self->rx);
        func_0057D8A0(&sz, &cz, self->rz);
        float ic = 1.0f / cx;
        if (!D_00621D08) {
            initializeDrift(D_008456B0, &D_00621D08);
        }
        float h = dt * D_008456B0[0];
        float k = (wx * sz - wy * cz) * ic;
        self->rx += -(wx * cz + wy * sz) * h;
        self->ry += k * h;
        self->rz += -(k * sx + wz) * h;
        for (i = 0; i < 3; i++) {
            float g = dt * D_006A3960[i];
            self->wx += (torque.x + (Iy - Iz) * wy * wz) * g / Ix;
            self->wy += (torque.y + (Iz - Ix) * wz * wx) * g / Iy;
            self->wz += (torque.z + (Ix - Iy) * wx * wy) * g / Iz;
            func_0057D8A0(&sx, &cx, self->rx);
            func_0057D8A0(&sz, &cz, self->rz);
            ic = 1.0f / cx;
            wx = self->wx;
            wy = self->wy;
            wz = self->wz;
            h = dt * D_008456B0[i + 1];
            k = (wx * sz - wy * cz) * ic;
            self->rx += -(wx * cz + wy * sz) * h;
            self->ry += k * h;
            self->rz += -(k * sx + wz) * h;
        }
        if (self->rx < 0.0f)
            self->rx += 0x1.921fb4p+2f;
        if (self->rx >= 0x1.921fb4p+2f)
            self->rx -= 0x1.921fb4p+2f;
        if (self->ry < 0.0f)
            self->ry += 0x1.921fb4p+2f;
        if (self->ry >= 0x1.921fb4p+2f)
            self->ry -= 0x1.921fb4p+2f;
        if (self->rz < 0.0f)
            self->rz += 0x1.921fb4p+2f;
        if (self->rz >= 0x1.921fb4p+2f)
            self->rz -= 0x1.921fb4p+2f;
        float im = 1.0f / sh->mass;
        self->pos.x += self->vel.x * dt;
        self->pos.y += self->vel.y * dt;
        self->pos.z += self->vel.z * dt;
        self->vel.x += force.x * im * dt;
        self->vel.y += force.y * im * dt;
        self->vel.z += force.z * im * dt;
    }
    func_005FE3A0(self);
    func_003E3290(self);
    func_003E3458(self, pts);
    if (!rest) {
        float x0, x1, z0, z1;
        if (old.x < self->pos.x) {
            x0 = old.x;
            x1 = self->pos.x;
        } else {
            x1 = old.x;
            x0 = self->pos.x;
        }
        if (old.z < self->pos.z) {
            z0 = old.z;
            z1 = self->pos.z;
        } else {
            z1 = old.z;
            z0 = self->pos.z;
        }
        HitCtx ctx;
        HitCtx *pc = &ctx;
        pc->from = &old;
        pc->body = self;
        pc->pts = pts;
        pc->hit = 0;
        func_00395A38(D_006D6060, (void *)func_003E26D0, pc, self->runway, x0, z0, x1, z1);
    }
    if (!rest) {
        if (!func_003E29F8(self))
            return 0;
        func_003E3748(self, pts, n);
    }
    func_003E3C48(self, pts, n);
    return 1;
}
