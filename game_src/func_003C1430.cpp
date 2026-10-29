/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native copy of the provisional draft for 003C1430: the slot-4 virtual gets flag and dt, which the EE forwards to it in $a1 and $f12 (passthrough_lint) */
struct VEntry {
    short delta;
    short index;
    void (*fn)(void *);
};

struct SparkParticle {
    int f0;
    struct VEntry *vtbl;
    short f8;
    short seed;
    float px, py, pz;
    float f18;
    float vx, vy, vz;
    float scale;
    float drag;
    float nx, ny, nz, d;
    float life;
    float total;
};

struct SparkParams {
    float drag;
    float gravity;
    float bounce;
    float friction;
    char pad10[0x34 - 0x10];
    float decay;
    char pad38[0x88 - 0x38];
    float low;
    float high;
    float chance;
    float boost;
};

extern struct SparkParams D_00621B50;
extern "C" float func_004AC400(void);

static inline float rsqrt(float a, float b)
{
    float r;
    __asm__("rsqrt.s %0, %1, %2" : "=f"(r) : "f"(a), "f"(b));
    return r;
}

static inline float drag(float k, float v)
{
    float r = k * v * v;
    if (v < 0.0f)
        r = -r;
    return r;
}

extern "C" int func_003C1430(struct SparkParticle *s, int flag, float dt)
{
    if (flag) {
        struct VEntry *e = s->vtbl + 4;
        ((void (*)(void *, int, float))e->fn)((char *)s + e->delta, flag, dt);
    }
    int ret = 0;
    float life = s->life - dt;
    s->life = life;
    if (!(life < 0.0f)) {
        struct SparkParams *p = &D_00621B50;
        float d0 = s->drag;
        float k = -d0 * d0 * p->drag;
        float vx = s->vx;
        float vy = s->vy;
        float vz = s->vz;
        float step = s->scale * dt;
        s->px = s->px + vx * dt;
        float age = s->total - life;
        k *= step;
        s->py = s->py + vy * dt;
        s->pz = s->pz + vz * dt;
        float ax = drag(k, vx);
        float g = p->gravity * dt;
        float ty = k * vy * vy;
        float ay;
        if (vy < 0.0f)
            ay = -ty - g;
        else
            ay = ty - g;
        float az = drag(k, vz);
        if (p->low < age && age < p->high) {
            if (func_004AC400() < p->chance)
                ay += p->boost * (float)s->seed * (1.0f / 1024.0f) * step;
        }
        s->vx += ax;
        s->vy += ay;
        s->vz += az;
        float dist = s->px * s->nx + s->py * s->ny + s->pz * s->nz + s->d;
        if (dist < 0.0f) {
            float dot = s->vx * s->nx + s->vy * s->ny + s->vz * s->nz;
            float tx, ty, tz, bx, by, bz, damp, bounce;

            s->life *= p->decay;
            s->px -= dist * s->nx;
            s->py -= dist * s->ny;
            s->pz -= dist * s->nz;
            ty = s->vy - dot * s->ny;
            tx = s->vx - dot * s->nx;
            tz = s->vz - dot * s->nz;
            bounce = p->bounce;
            damp = 1.0f - rsqrt((bounce + bounce) * p->friction * dot, tx * tx + ty * ty + tz * tz);
            bx = -bounce * s->vx;
            by = -bounce * s->vy;
            bz = -bounce * s->vz;
            if (damp < 0.0f) {
                s->vx = bx;
                s->vy = by;
                s->vz = bz;
            } else {
                tx *= damp;
                ty *= damp;
                tz *= damp;
                s->vx = tx + bx;
                s->vy = ty + by;
                s->vz = tz + bz;
            }
        }
        ret = 1;
    }
    return ret;
}
