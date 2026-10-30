/* The native graphics interface for game code (runtime/native/pgl.cpp; the types are include/gt4/gfx.h's,
 * restated for the game's compiler): game code draws by calling these instead of writing GS packets. */
#ifndef GT4_GFX_H
#define GT4_GFX_H

/* A vertex: position in 1/16 pixel with the drawing context's origin, depth, colour (R in the low byte,
 * alpha 0x80 = 1), texture coordinates S, T, Q or U, V (1/16 texel), fog, and whether it draws (0: the
 * first vertices of a strip). */
struct gt4_vertex {
    unsigned short x, y;
    unsigned int z;
    unsigned int rgba;
    float s, t, q;
    unsigned short u, v;
    unsigned char fog;
    unsigned char kick;
    unsigned short pad;
};

/* gt4_gfx_draw flags: set the primitive attributes first; Z has 24 bits and the vertices carry fog; the
 * vertices carry their colour (and Q), S T Q, U V. */
#define GT4_DRAW_PRIM 1
#define GT4_DRAW_FOG 2
#define GT4_DRAW_COLOR 4
#define GT4_DRAW_ST 8
#define GT4_DRAW_UV 16

#ifdef __cplusplus
extern "C" {
#endif
/* Primitives into the frame the graphics library at `context` (0x70002000) is building, in order with
 * what it has written so far. */
void gt4_gfx_draw(void *context, unsigned int prim, unsigned int flags, const struct gt4_vertex *v, int n);
/* Drawing state in the library's own encoding (its shadow at +0x120/+0x170 holds GS register values):
 * `n` {value, register address} pairs, applied in order. */
void gt4_gfx_set(void *context, const unsigned long long *pairs, int n);
/* Pixels at `src` into graphics memory: `destination` = base (blocks) | width (64-pixel units) << 16 | format
 * << 24, the rectangle (x, y, w, h). In the frame's order, or (_now) at once. Returns the size in qwords. */
int gt4_gfx_upload(void *context, unsigned int destination, int x, int y, int w, int h, const void *src);
int gt4_gfx_upload_now(unsigned int destination, int x, int y, int w, int h, const void *src);
#ifdef __cplusplus
}
#endif

/* The library's packed Z and fog word (Z in the low 24 bits, fog in the top 8) into a vertex. */
static inline void gt4_vertex_zf(struct gt4_vertex *v, unsigned int zf)
{
    v->z = zf & 0xFFFFFF;
    v->fog = (unsigned char)(zf >> 24);
}

#endif
