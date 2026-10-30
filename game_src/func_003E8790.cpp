/* compiler: ee-gcc2.96-nsa-nosib-as2004 */
/* Native version of 003E8790 (provisional draft with VU0 asm): draws one small particle as a quad turned by its
 * angle, clipped against its plane. The screen matrix s->matrix points to goes into shared vectors 20..23, and the
 * record's plane (four floats) through it gives the plane in view space (all four lanes, w from the record). A
 * plane facing away (w < 0), or one with no corner or every corner behind it, leaves the whole quad
 * (func_003E8728); otherwise the corners behind it are moved (func_003E8C70), the two crossing edges cut
 * (func_003E8CD8, func_003E8DE0) and the two parts drawn (func_003E8E60, func_003E8F68, func_003E8438): the
 * moved part with the record's colour word as rgba2, the rest with func_003E8BA8's colour only. */
#include "gt4_vec.h"
extern "C" void *memcpy(void *, const void *, unsigned int);
typedef unsigned int u128 __attribute__((mode(TI)));
struct R { unsigned char flags[4]; int color; float size; float life,scale,angle; char pad[24]; float plane[4]; };
struct E { float x,y,z; int index; };
struct Self { R *r; char pad[0x40]; float *uv,*vert,*plane; u128 *matrix; };
extern float D_006A3B30[4][8];
extern "C" {
int func_003E8BA8(Self *,E *); void func_004A5F00(float *,float *,float);
void func_003E8728(Self *,float *,int,int) throw();
void func_003E8438(Self *,float *,float *,int,int,int) throw();
void func_003E8C70(Self *,float *,float *);
float func_003E8CD8(Self *,float *,int);
void func_003E8DE0(Self *,float *,float *,float);
int func_003E8E60(Self *,int,int,float *,float *,float *,float *,float *);
void func_003E8F68(Self *,int,float *,float *);
}

extern "C" void func_003E8790(Self *s, E *e) {
    float verts[12], sc[2], plane[4], moved[12];
    float edge0[8], edge1[8], result[16], uv[12];
    R *r = s->r + e->index;
    const int color = func_003E8BA8(s, e);
    const float diameter = r->size + r->size;
    func_004A5F00(&sc[0], &sc[1], r->angle);
    sc[0] *= r->size;
    sc[1] *= r->size;
    const float x = e->x, y = e->y, z = e->z;
    const float b = sc[1] - sc[0], a = sc[1] + sc[0];
    verts[0] = x + b; verts[1] = y + a; verts[2] = z;
    verts[3] = x - a; verts[4] = y + b; verts[5] = z;
    verts[6] = x - b; verts[7] = y - a; verts[8] = z;
    verts[9] = x + a; verts[10] = y - b; verts[11] = z;
    float pl[4];
    memcpy(pl, r->plane, 16);
    memcpy(gt4_vec(20), s->matrix, 64);   /* lqc2 $vf20..23 */
    gt4_rows4(plane, gt4_vec(20), gt4_vec(21), gt4_vec(22), gt4_vec(23), pl, pl[3], 4);
    const int type = r->flags[2] >> 6;
    s->uv = D_006A3B30[type];
    s->vert = verts;
    s->plane = plane;
    int mask = 0;
    if (!(plane[3] < 0.0f)) {
        int i, bit;
        for (i = 0, bit = 1; i < 12; i += 3, bit <<= 1) {
            float *p = verts + i;
            if (p[0] * plane[0] + p[1] * plane[1] + p[2] * plane[2] + plane[3] < 0.0f) {
                mask |= bit;
                func_003E8C70(s, moved + i, p);
            }
        }
    }
    if (plane[3] < 0.0f || mask == 0 || mask == 15) {
        func_003E8728(s, verts, color, type);
        return;
    }
    int end = -1, start = -1;
    mask <<= 4;
    int i, pair, bit;
    for (i = 0, pair = 0x33, bit = 0x10; i < 4; ++i, pair <<= 1, bit <<= 1) {
        const int m = mask & pair;
        if (m == 0 || m == (pair & 0xF0)) continue;
        if (m == bit) {
            start = i;
            const float t = func_003E8CD8(s, edge1, start);
            func_003E8DE0(s, edge1, moved + start * 3, diameter * t);
        } else {
            end = i;
            const float t = func_003E8CD8(s, edge0, end);
            func_003E8DE0(s, edge0, moved + end * 3 + 3, diameter * (1.0f - t));
        }
    }
    int n = func_003E8E60(s, end, start, result, uv, edge0, edge1, moved);
    func_003E8F68(s, n, result, uv);
    func_003E8438(s, result, uv, color, r->color, n);
    n = func_003E8E60(s, start, end, result, uv, edge1, edge0, verts);
    func_003E8F68(s, n, result, uv);
    func_003E8438(s, result, uv, color, color, n);
}
