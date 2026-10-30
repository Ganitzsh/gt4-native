/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 00497838 (provisional draft build/swarm/o2/00497838_best.cpp, checked against the original): draws
 * a pre-built shape (its pieces: model packets in game memory) at (x, y, z) with the colours at `addr` and the word
 * `arg3`. With the lane mask C0C0C0C0 (004A72C0 sets it when +0x40 differs), each piece binds its texture
 * (0049CC48), is decoded into a 3D batch and run by the renderer at the batch size (+0x14) + 0x3C (less the
 * constants and state the piece adds) with lighting chain 0x2DF; the first two pieces also get the colours (four
 * V4-8 quadwords at 0xCB), the word at 0xCA and the position at 0xC9. The original wrote DMA ref and cnt tags with
 * the VIF codes (ITOP, MSCAL, UNPACK); natively these are batch records (compat/game/gt4_gfx.h). */
#include "spr.h"
#include "gt4_gfx.h"
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
typedef unsigned long u64;
struct State {
    u8 active; char pad1[2]; u8 dirty; char pad4[0x10]; int count; char pad18[0x28];
    u32 magic; char pad44[0x194]; u64 flags; char pad1e0[0x18]; u32 *cursor;
    char pad1fc[0x1020 - 0x1fc]; int a, b, c;
};
struct Piece { u32 addr; u16 size, index; };
struct Info { char pad[0xA]; u16 count, a, b; Piece pieces[1]; };
struct Resource { char pad[0x18]; void *handle; };
struct Owner { char pad[0x28]; Resource *res[1]; };
union Word { float f; u32 i; };
extern "C" void func_004A72C0(void *, unsigned int);
extern "C" void func_0049E480(State *);
extern "C" void func_0049CC48(void *);
extern "C" void func_0049E548(State *, int, float);
extern "C" void func_0049E5F8(State *, u32);
extern "C" void func_00497838(Owner *owner, Info *info, u32 addr, u32 arg3, float x, float y, float z) {
    State *s = (State *)spr_base();
    if (s->magic != 0xC0C0C0C0) func_004A72C0(s, 0xC0C0C0C0);
    if (s->active) func_0049E480(s);
    int blocks = s->count;
    if (blocks == 0) return;
    blocks += 0x3C;
    s->a += info->a;
    s->b += info->b;
    s->c += info->count;
    int next = 0;
    u32 i;
    Piece *piece = info->pieces;
    for (i = 0; i < info->count; i++, piece++) {
        if (piece->index) {
            Resource *r = owner->res[piece->index];
            if (r) func_0049CC48(r->handle);
        }
        if (next) gt4_gfx_batch_run(s, (u32)next & 0xFFFF, 0x2DF);
        gt4_gfx_batch_model(s, (const void *)(piece->addr & 0x0FFFFFFF), piece->size, s->magic);
        next = blocks;
        if (i < 2) {
            /* four colours (V4-8, unsigned), the word, the position (V3-32: w 0) */
            const u8 *c = (const u8 *)(addr & 0x0FFFFFFF);
            u32 colours[16];
            for (int k = 0; k < 16; k++) colours[k] = c[k];
            gt4_gfx_batch_write(s, 0xCB, colours, 4, 0xF);
            u32 word[4];
            word[0] = word[1] = word[2] = word[3] = arg3;
            gt4_gfx_batch_write(s, 0xCA, word, 1, 0xF);
            Word pos[4];
            pos[0].f = x;
            pos[1].f = y;
            pos[2].f = z;
            pos[3].i = 0;
            gt4_gfx_batch_write(s, 0xC9, pos, 1, 0xF);
        }
        if (!s->dirty) {
            next--;
            func_0049E548(s, 0xC1, 0.0f);
            s->dirty = 1;
        }
        if (s->flags & 0x000003FF00001FFFul) {
            func_0049E5F8(s, 0xD9);
            next -= 5;
        }
    }
    if (next) gt4_gfx_batch_run(s, (u32)next & 0xFFFF, 0x2DF);
}
