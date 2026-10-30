/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 003E34E8 (provisional draft with VU0 asm): first leaves the transpose of the body's matrix
 * (shared vectors 20..23) in 24..27, then pushes the body out of the ground plane (its normal and distance at 0x34):
 * for each of `count` points below it, a spring force (depth * -10 along the normal) plus damping (-0.999 times the
 * body's velocity at its shape's matching contact point) is added to out, and the body is finally moved back along
 * the normal by the deepest point's depth. */
#include "gt4_vec.h"
extern "C" void *memcpy(void *, const void *, unsigned int);
struct Vec {float x,y,z;};
struct Body {int v;Vec rot,pos,vel,angular;float nx,ny,nz,d;unsigned char idx;};
extern char* D_006D6054;
extern "C" void func_003E2140(Vec*,Vec*,void*) throw();
extern "C" void func_003E21D8(Vec*,Vec*) throw();

/* shared vectors 24..27 = the transpose of 20..23 (vmove, then qmfc2/pextlw/pextuw/pcpyld/pcpyud/qmtc2): every
 * lane, copied as bits */
static inline void transpose_20_to_24() {
    unsigned int m[4][4], t[4][4];
    memcpy(m, gt4_vec(20), 64);
    for (int r = 0; r < 4; r++)
        for (int k = 0; k < 4; k++) t[k][r] = m[r][k];
    memcpy(gt4_vec(24), t, 64);
}

extern "C" void func_003E34E8(Body *self, Vec *out, int arg2, Vec *points, int count) {
    Vec force, local, velocity;
    char *data = D_006D6054 + self->idx * 160 + 32;
    transpose_20_to_24();
    float minimum = 0.0f;
    char *p = data + 16;
    Vec *cursor = points;
    for (int remaining = count; remaining > 0; --remaining, p += 16) {
        const float nx = self->nx, ny = self->ny, nz = self->nz;
        const float dist = cursor->x * nx + cursor->y * ny + cursor->z * nz + self->d;
        ++cursor;
        if (dist < 0.0f) {
            if (dist < minimum) minimum = dist;
            const float scale = dist * -10.0f;
            force.x = nx * scale;
            force.y = ny * scale;
            force.z = nz * scale;
            func_003E2140(&local, &self->angular, p);
            func_003E21D8(&velocity, &local);
            velocity.x += self->vel.x;
            velocity.y += self->vel.y;
            velocity.z += self->vel.z;
            force.x += velocity.x * -0x1.ff7cecp-1f;   /* 0xBF7FBE76, -0.999 */
            force.y += velocity.y * -0x1.ff7cecp-1f;
            force.z += velocity.z * -0x1.ff7cecp-1f;
            out->x += force.x;
            out->y += force.y;
            out->z += force.z;
        }
    }
    if (minimum != 0.0f) {
        self->pos.x -= minimum * self->nx;
        self->pos.y -= minimum * self->ny;
        self->pos.z -= minimum * self->nz;
    }
}
