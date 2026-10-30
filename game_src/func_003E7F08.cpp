/* compiler: ee-gcc2.96-nsa-nosib-as2004 */
/* Native version of 003E7F08 (provisional draft with VU0 asm): draws a particle set. Loads its transform
 * (func_004A7C38) and copies the projection matrix (func_004A5348(1), func_004A7550); walks the linked records
 * (of `group`, or all when it is negative): each centre through the current matrix (shared vectors 28..31, w = 1)
 * into view space, kept between 0.01 and the config's far distance when its projected circle (size * 1.2071068)
 * touches the [-1, 1] screen square and is under 2.4142134 wide. Sorts the kept ones by depth (unless the config's
 * mode is 0x48), sets up the GS state and the screen matrix (into the stack buffer s->matrix points to while the
 * particles are drawn), sets the primitive (func_004A5BE0(4)) and draws each one (func_003E8610, or 003E8790 for size
 * 3 or less in mode 0). The prefetches (pref) are left out. The original opened a DIRECT packet that the particles
 * filled; natively the primitive and each particle are draw records (compat/game/gt4_gfx.h). */
#include "gt4_gfx.h"
#include "gt4_vec.h"
typedef unsigned long u64;
struct E { float x,y,z; int index; };
struct R { signed next:13; unsigned group:3; unsigned rest:16; int color; float size; char pad[20]; float xyz[3]; char tail[20]; };
struct Config { char pad[32]; float far; char pad24[0x8f4]; int mode; };
struct Texture { char pad[24]; void *image; };
struct Self { R *r; short head; char pad6[46]; Texture *tex; Config *cfg; char pad3c[20]; void *matrix; u64 *start,*cursor; float transform[16]; };
extern "C" {
void func_004A7C38(float *); void func_004A5348(int); void func_004A7550(void *);
void func_003E9018(Self *, E *, int, int);
void func_004AB040(int); void func_004A1638(int); void func_004A29A8(int); void func_004A2808(int,float);
int func_00499728(void *); void func_0049CC48(void *); void func_004A4550(int,int); void func_0049FE28(); void func_004A53F8(); void func_004A7A54(); void func_004A7A00(); void func_004A7454();
void *func_004A5B90(unsigned); unsigned func_004A5BE0(int); void func_004A5400();
void func_003E8610(Self *,E *); void func_003E8790(Self *,E *);
}
static inline u64 *alloc(u64 **pp) { return (*pp+=2)-2; }

extern "C" void func_003E7F08(Self *s, int mode, int group) {
    float m[16];
    E arr[256];
    float matrix[20] __attribute__((aligned(16)));
    if (!s->tex) return;
    func_004A7C38(s->transform);
    func_004A5348(1);
    func_004A7550(m);
    func_004A5348(0);
    const float near = 0.01f;   /* 0x3C23D70A */
    Config *c = s->cfg;
    E *out = arr;
    const float far = c->far;
    for (int i = s->head; i >= 0; i = s->r[i].next) {
        if (group >= 0 && (int)s->r[i].group != group) continue;
        float r[3];
        gt4_rows4(r, gt4_vec(28), gt4_vec(29), gt4_vec(30), gt4_vec(31), s->r[i].xyz, 1.0f, 3);
        out->x = r[0];
        out->y = r[1];
        out->z = r[2];
        const float z = out->z;
        if (!(near < -z) || !(-z < far)) continue;
        const float inv = -1.0f / z;
        const float radius = s->r[i].size * 0x1.3504f2p+0f * inv;   /* 0x3F9A8279 */
        const float vx = out->x, vy = out->y;
        const float rx = m[0] * radius, ry = m[5] * radius;
        const float x = (vx * m[0] + vy * m[4] + z * m[8]) * inv;
        const float y = (vx * m[1] + vy * m[5] + z * m[9]) * inv;
        if (rx < 0x1.3504f2p+1f && x - rx < 1.0f && x + rx > -1.0f && y - ry < 1.0f && y + ry > -1.0f) {   /* 0x401A8279 */
            out->index = i;
            ++out;
        }
    }
    const int count = out - arr;
    if ((c->mode ^ 0x48) != 0) func_003E9018(s, arr, 0, count - 1);
    func_004AB040(4);
    func_004A1638(6);
    func_004A1638(5);
    func_004A1638(2);
    func_004A29A8(0);
    func_004AB040(8);
    if (s->cfg->mode == 0x48) func_004A2808(0x48, 1.0f);
    else func_004A2808(0x44, 1.0f);
    func_00499728(s->tex);
    func_0049CC48(s->tex->image);
    func_004A4550(0, 1);
    func_004A4550(1, 0);
    func_004A4550(4, 1);
    func_004A4550(5, 1);
    func_0049FE28();
    func_004A53F8();
    s->matrix = matrix;
    func_004A7A54();
    func_004A7A00();
    func_004A7550(s->matrix);
    func_004A7454();
    func_0049FE28();
    gt4_gfx_draw((void *)0x70002000, func_004A5BE0(4), GT4_DRAW_PRIM, 0, 0);
    switch (mode) {
    case 0:
        for (int i = 0; i < count; ++i) {
            if (s->r[arr[i].index].size > 3.0f) func_003E8610(s, arr + i);
            else func_003E8790(s, arr + i);
        }
        break;
    case 1:
        for (int i = 0; i < count; ++i) func_003E8610(s, arr + i);
        break;
    }
    func_004A5400();
    func_004AB040(6);
    func_004A29A8(1);
}
