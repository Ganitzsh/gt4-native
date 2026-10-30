/* compiler: ee-gcc2.96-nsa-nosib-as2004 */
/* Native version of 003B8EA0 (provisional draft with VU0 asm, build/swarm/v1/003b8ea0_best.cpp): draws a light's
 * glow as one quad. The current matrix moved to the light (func_004A7844) gives its view position (shared vector
 * 31) and second row (29). In front of the camera (z + 0.5 <= 0), its direction through the matrix (28..31, w
 * from the light) against the view position gives the intensity (rsqrt.s; none when negative); the quad's four
 * corners (at depth z + 0.5, sized by the intensity and the light's width and height) and its clamped colour go
 * to func_0049C0D8. */
#include "gt4_vec.h"
struct Vec { float x,y,z,w; };
struct Light { char pad[32]; Vec direction; float x,y,z,p3c,width; char p44[16]; float r,g,b; };
struct Self { char pad[0x240c]; float r,g,b,a,bottom,height,offset; };
extern "C" {
void func_004A53F8(); void func_004A5400(); void func_004A7844(float,float,float);
void *func_004A5B70(int); void func_004A7454();
int func_0049C0D8(int,void *,void *,void *,int,int);
extern char D_006A2660[];
}

/* the EE FPU's rsqrt.s: a / sqrt(b), the largest float of a's sign where b is 0 */
static inline float ee_rsqrt(float a, float b) { return gt4_vu_div(a, __gt4_sqrtf(b)); }

extern "C" void func_003B8EA0(Self *s, Light *l, float alpha) {
    float p[3], v[3];
    func_004A53F8();
    func_004A7844(l->x, l->y, l->z);
    const float *t = gt4_vec(31), *row = gt4_vec(29);
    p[0] = t[0];
    p[1] = t[1];
    p[2] = t[2];
    const float qx = row[0], qy = -row[1];
    const float z = p[2] + 0.5f;
    if (z > 0.0f) {
        func_004A5400();
        return;
    }
    float dir[4];
    dir[0] = l->direction.x;
    dir[1] = l->direction.y;
    dir[2] = l->direction.z;
    dir[3] = l->direction.w;
    gt4_rows4(v, gt4_vec(28), gt4_vec(29), gt4_vec(30), gt4_vec(31), dir, dir[3], 3);
    const float d = ee_rsqrt(v[0] * p[0] + v[1] * p[1] + v[2] * p[2], p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
    if (d < 0.0f) {
        func_004A5400();
        return;
    }
    alpha *= d;
    const float k = z / p[2];
    const float x = p[0] * k, y = p[1] * k;
    float *out = (float *)func_004A5B70(3);
    int *colors = (int *)func_004A5B70(1);
    func_004A7454();
    float h = s->height;
    const float ly = l->y;
    const float w = ((s->a - s->bottom) * d + s->bottom) * l->width;
    if (ly < h) h = ly;
    const float nw = -w, nh = -h, no = -s->offset;
    out[11] = z;
    out[2] = z;
    out[5] = z;
    out[8] = z;
    const float wx = nw * qx, hx = nh * qx, ox = no * qx, oy = no * qy, hy = nh * qy, wy = w * qy, px = w * qx,
                ny = nw * qy;
    out[9] = x + (ny - ox);
    out[3] = x + (ny - hx);
    out[0] = x + (wy - hx);
    out[1] = y + (px + hy);
    out[6] = x + (wy - ox);
    out[7] = y + (px + oy);
    out[10] = y + (wx + oy);
    out[4] = y + (wx + hy);
    float r = l->r * alpha * s->r, b = l->b * alpha * s->b, g = l->g * alpha * s->g;
    if (r > 255.0f) r = 255.0f;
    if (g > 255.0f) g = 255.0f;
    if (b > 255.0f) b = 255.0f;
    const int c = (int)r | ((int)g << 8) | ((int)b << 16);
    colors[0] = c;
    colors[3] = c;
    colors[2] = c;
    colors[1] = c;
    func_0049C0D8(4, out, D_006A2660, colors, 0, 0);
    func_004A5400();
}
