/* compiler: ee-gcc2.96-nsa-nosib-as2004 */
/* Native version of 003E2710 (provisional draft with VU0 asm): places a body: its shape index, position and
 * rotation from in, its motion cleared, the ground under it looked up (func_003E29F8, one unit higher). Unless its
 * shape's flag 2 says not to, its rotation is rebuilt (func_003E2268 from the ground normal, turned by in's angles)
 * and loaded with its position into shared vectors 20..23 (w of 23: the EE's sign extension of z's word), and the
 * body is dropped so that its shape's contact point (the shape entry's four floats at +0x10, through 20..23) rests
 * on the ground plane. */
#include "gt4_vec.h"
extern "C" void *memcpy(void *, const void *, unsigned int);
typedef unsigned int u32;
typedef unsigned int u128 __attribute__((mode(TI)));
struct Vec3 {float x,y,z;};
struct Raw {int b[3];};
struct Obj {int v; Vec3 rot,pos,a,b; float nx,ny,nz,d; unsigned char idx,flag; signed char c46,c47; int c48,c4c,c50,c54; unsigned char c58,c59,c5a;};
struct Input {unsigned char idx;char pad[7];Vec3 pos,rot;};
extern char *D_006D6054;
extern Raw D_006A3950;
extern "C" void func_003E2268(u128*,Raw*,float*) throw();
extern "C" void func_003E24F0(u128*,float,float,float) throw();
extern "C" void func_003E2600(Vec3*,u128*) throw();
extern "C" void func_003E29C8(Obj*);
extern "C" u32 func_003E29F8(Obj*);
extern "C" void func_003E4930(Obj*,u128*);
extern "C" void func_004A53F8();
extern "C" void func_004A5400();
extern "C" void func_004A7454();
extern "C" void func_004A7550(u128*);
extern "C" void func_004A7698(u128*);

extern "C" void func_003E2710(Obj *self, Input *in) {
    Raw raw;
    u128 m[4];
    self->idx = in->idx;
    self->pos.x = in->pos.x;
    self->pos.y = in->pos.y;
    self->pos.z = in->pos.z;
    self->rot.x = in->rot.x;
    self->rot.y = in->rot.y;
    self->rot.z = in->rot.z;
    self->a.x = 0;
    self->a.y = 0;
    self->a.z = 0;
    self->b.x = 0;
    self->b.y = 0;
    self->b.z = 0;
    func_003E29C8(self);
    self->c58 = 2;
    self->c59 = 2;
    self->c5a = 2;
    self->flag = 0;
    char *data = D_006D6054 + self->idx * 160 + 32;
    self->pos.y += 1.0f;
    self->c48 = -1;
    self->c46 = -128;
    self->c47 = -128;
    func_003E29F8(self);
    self->pos.y -= 1.0f;
    unsigned int flags = (unsigned char)data[2];
    if (((flags >> 1) & 1) == 0) {
        raw = D_006A3950;
        func_003E2268(m, &raw, &self->nx);
        func_004A53F8();
        func_004A7454();
        func_004A7698(m);
        func_003E24F0(m, in->rot.x, in->rot.y, in->rot.z);
        func_004A7698(m);
        func_004A7550(m);
        func_004A5400();
        func_003E2600(&self->rot, m);
        func_003E4930(self, m);

        u32 z, w;
        memcpy(gt4_vec(20), &m[0], 16);
        memcpy(gt4_vec(21), &m[1], 16);
        memcpy(gt4_vec(22), &m[2], 16);
        memcpy(gt4_vec(23), &self->pos, 12);
        memcpy(&z, &self->pos.z, 4);
        w = (z & 0x80000000u) ? 0xFFFFFFFFu : 0u;
        memcpy(gt4_vec(23) + 3, &w, 4);

        float p[4], out[3];
        memcpy(p, data + 16, 16);
        gt4_rows4(out, gt4_vec(20), gt4_vec(21), gt4_vec(22), gt4_vec(23), p, p[3], 3);
        self->pos.y += -((out[0] * self->nx + out[2] * self->nz) + self->d) / self->ny - out[1];
    }
    self->c4c = 0;
    self->c50 = 0;
    self->c54 = 0;
}
