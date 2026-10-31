/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0049BE10 (from its draft, checked against the assembly): adds a strip of `n` vertices (3..64)
 * to the immediate batch 0049BBB0 opens and 0049BBF0 draws (through 0049C0D8: light glows 003B8EA0, billboard
 * sets): its positions (V3-32) at the batch's next vertex, its texture coordinates (V2-32, at 0x40 on), colours
 * (V4-8, at 0x80 on) and normals (V3-32, at D_0084AF3C on), each when given, and its strip lengths for 0049BBF0. A
 * batch that would pass 64 vertices is drawn first and a new one begun. The original sent each array as a DMA
 * reference with its UNPACK in the tag; natively they go as batch records (compat/game/gt4_gfx.h), the same
 * formats expanded by gt4_gfx_batch_unpack, copied when the strip is added. */
#include "gt4_gfx.h"
typedef unsigned char u8;
typedef unsigned int u32;
struct Obj { char pad[0x14]; int active; char pad18[0x40 - 0x18]; u32 mask; char pad44[0x1020 - 0x44]; int vertices; unsigned int triangles; };
extern int D_0084AF30, D_0084AF34, D_0084AF38, D_0084AF3C;
extern u8 D_0084AF40[];
extern u8 D_00624C70;
extern "C" int func_0049BBF0() throw();
extern "C" void func_0049BE10(Obj *p, int n, int positions, int tex, int colors, int normals, u8 *counts) {
    if (p->active && n >= 3) {
        if (n < 65) {
            int aligned = (n + 3) & ~3;
            int base = D_0084AF30;
            if (base + aligned >= 65) {
                func_0049BBF0();
                D_00624C70 = 1;
                base = 0;
                D_0084AF34 = 0;
                D_0084AF38 = 0;
                D_0084AF3C = 0;
                D_0084AF30 = 0;
            }
            gt4_gfx_batch_unpack(p, base, 0x68, aligned, 1, 4, 4, p->mask, 0, (const void *)(positions & 0x0fffffff));
            if (tex)
                gt4_gfx_batch_unpack(p, D_0084AF30 + 64, 0x64, (n + 1) & ~1, 1, 4, 4, p->mask, 0, (const void *)(tex & 0x0fffffff));
            if (colors)
                gt4_gfx_batch_unpack(p, D_0084AF30 + 128, 0x6E, aligned, 1, 4, 4, p->mask, 0, (const void *)(colors & 0x0fffffff));
            if (normals && D_0084AF3C)
                gt4_gfx_batch_unpack(p, D_0084AF3C + D_0084AF30, 0x68, aligned, 1, 4, 4, p->mask, 0,
                                     (const void *)(normals & 0x0fffffff));
            p->vertices += n;
            D_0084AF30 += n;
            if (counts) {
                int total = *counts++;
                for (int i = 0; i < total; i++) {
                    D_0084AF40[D_0084AF34] = counts[i];
                    p->triangles += counts[i] - 2;
                    D_0084AF34++;
                }
            } else {
                p->triangles += n - 2;
                D_0084AF40[D_0084AF34++] = n;
            }
        }
    }
}
