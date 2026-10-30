/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 003B8530 (provisional draft with VU0 asm, build/swarm/v1/003b8530_best.cpp): adds a light to
 * the flare list (at most 100). Its origin projected and tested on screen (func_003B7C98, sized by the larger of
 * width and height); a light whose width and height differ by 0.01 or more gets its stretch from its direction
 * through the current matrix (shared vectors 28..31, w from the light) and is never faced; its normal through the
 * rotation (28..30) gives the facing strength (func_003B8460) and the entry's n; its position through the matrix
 * (w = 1) the entry's position.
 * Native correction kept from the earlier native copy (argument order, argcheck.py orders): its matched caller
 * RaceCarModel__virtual_35 passes (self, light, float alpha, int flag); the draft's order is the same EE function
 * but natively read alpha and flag from each other's stack slots. */
#include "gt4_vec.h"
struct Vec { float x,y,z,w; };
struct V3 { float x,y,z; };
struct Light { Vec dir; char pad[16]; Vec normal; Vec pos; float width,height; V3 origin; float r,g,b; unsigned power; };
struct Entry { V3 pos; Light *light; int flag; float a,dx,dy,n; };
struct Param { float r,g,b,sx,sy,a,p18,c,b1,a2,p28,p2c,blink,phase,p38; };
struct Self { char pad[0xa30]; int type,count; Entry entries[100]; char gap[0x21f0-0x1848]; Param param; };
extern "C" {
int func_003B7C98(Self *,Vec *,Vec *,V3 *,float,float) throw();
float func_003B8460(Self *,Vec *,V3 *,float) throw();
unsigned func_003B7EE8(float,Self *,Vec *,float) throw();
}

/* the EE FPU's rsqrt.s: a / sqrt(b), the largest float of a's sign where b is 0 */
static inline float ee_rsqrt(float a, float b) { return gt4_vu_div(a, __gt4_sqrtf(b)); }

extern "C" void func_003B8530(Self *s, Light *l, float alpha, int flag) {
    Vec p, q;   /* the origin in view space and on screen (func_003B7C98) */
    int count = s->count;
    if (count >= 100) return;
    float w = l->width, h = l->height;
    Param *pm = &s->param;
    Entry *e = &s->entries[count];
    float radius = pm->sy;
    if (w < h) radius *= h;
    else radius *= w;
    if (!func_003B7C98(s, &p, &q, &l->origin, radius, pm->p28)) return;
    w = l->width;
    h = l->height;
    float diff = w - h;
    if (diff < 0.0f) diff = -diff;
    if (!(diff < 0.01f)) {
        flag = 0;
        float d[4], t[2];
        d[0] = l->dir.x;
        d[1] = l->dir.y;
        d[2] = l->dir.z;
        d[3] = l->dir.w;
        gt4_rows4(t, gt4_vec(28), gt4_vec(29), gt4_vec(30), gt4_vec(31), d, d[3], 2);
        const float len = t[0] * t[0] + t[1] * t[1];
        if (len == 0.0f) {
            e->dx = w;
            e->dy = 0.0f;
        } else {
            const float k = (w - h) + ee_rsqrt(h, len);
            e->dy = t[1] * k;
            e->dx = t[0] * k;
        }
    } else {
        e->dy = 1.0f;
        e->dx = 1.0f;
    }
    float nv[3], n[3];
    nv[0] = l->normal.x;
    nv[1] = l->normal.y;
    nv[2] = l->normal.z;
    gt4_rows3(n, gt4_vec(28), gt4_vec(29), gt4_vec(30), nv, 3);
    V3 normal;
    normal.x = n[0];
    normal.y = n[1];
    normal.z = n[2];
    float strength = -1.0f;
    if (flag) strength = func_003B8460(s, &p, &normal, (float)l->power * pm->p38);
    if (strength < 0.0f) e->flag = 0;
    else {
        int f = 1;
        if (!s->type) f = func_003B7EE8(1.0f, s, &q, pm->p2c);
        e->flag = f;
    }
    float lp[3], wp[3];
    lp[0] = l->pos.x;
    lp[1] = l->pos.y;
    lp[2] = l->pos.z;
    gt4_rows4(wp, gt4_vec(28), gt4_vec(29), gt4_vec(30), gt4_vec(31), lp, 1.0f, 3);
    e->pos.x = wp[0];
    e->pos.y = wp[1];
    e->pos.z = wp[2];
    e->light = l;
    e->a = strength * alpha;
    e->n = normal.z * -0.5f;
    s->count++;
}
