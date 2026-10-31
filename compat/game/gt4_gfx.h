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
#define GT4_DRAW_ATTRIBUTES_ONLY 32   /* the vertices' colour, S T Q, U V are set; nothing is drawn */

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
/* GT4's 3D batches (include/gt4/gfx.h kBatch*): a frame starts; `count` qwords of `data` into the batch being
 * filled from qword `qword` (the lanes in `lanes`, bit k lane k); a model packet in game memory decoded into it
 * with the lane mask `mask`; the batch drawn by the renderer at `entry` with the lighting chain `itop`. */
void gt4_gfx_batch_frame(void *context);
void gt4_gfx_batch_write(void *context, unsigned int qword, const void *data, int count, unsigned int lanes);
void gt4_gfx_batch_model(void *context, const void *source, int qwords, unsigned int mask);
void gt4_gfx_batch_run(void *context, unsigned int entry, unsigned int itop);
/* `num` elements of the compact format `format` (include/gt4/unpack.h: 0x60 | components - 1 << 2 | 32/16/8-bit,
 * + 0x10 masked) from `data` into the batch from qword `qword`, unsigned when `usn`, with the cycle (cl, wl), the
 * lane `mask` and `row` (null: zeros). */
void gt4_gfx_batch_unpack(void *context, unsigned int qword, unsigned int format, int num, int usn, unsigned int cl,
                          unsigned int wl, unsigned int mask, const unsigned int *row, const void *data);
/* The display from the next VBlank on, in the library's encoding (the GS's display register values): bit k of
 * `which` gives values[k], k = 0 PMODE, 1 SMODE2, 2 DISPFB1, 3 DISPLAY1, 4 DISPFB2, 5 DISPLAY2, 6 BGCOLOR. */
void gt4_gfx_display(unsigned int which, const unsigned long long *values);
#define GT4_KEEP_ITOP 0xFFFFFFFFu
/* A screen grid (program 0x3D50 or 0x3EA0): `qwords` of `block` as GT4's grid programs read them (parameters at
 * qword 0, the packet template at qword 0 .y), drawn piece by piece. */
void gt4_gfx_grid(void *context, unsigned int program, const void *block, int qwords);
#ifdef __cplusplus
}
#endif

/* `n` three-float vectors into the batch from `qword` on, as the original's UNPACK V3-32 filled the quadwords:
 * unmasked (keep_w 0), lane w takes the next vector's x (0 after the last); masked with the lane mask C0C0C0C0
 * (keep_w 1), lane w keeps what it held. */
static inline void gt4_batch_v3(void *context, unsigned int qword, const float *v, int n, int keep_w)
{
    union { float f; unsigned int u; } c;
    unsigned int *q = (unsigned int *)__builtin_alloca((n > 0 ? n : 1) * 16);
    int k;
    for (k = 0; k < n; k++) {
        c.f = v[3 * k]; q[4 * k] = c.u;
        c.f = v[3 * k + 1]; q[4 * k + 1] = c.u;
        c.f = v[3 * k + 2]; q[4 * k + 2] = c.u;
        if (k + 1 < n) { c.f = v[3 * k + 3]; q[4 * k + 3] = c.u; }
        else q[4 * k + 3] = 0;
    }
    gt4_gfx_batch_write(context, qword, q, n, keep_w ? 0x7 : 0xF);
}

/* The library's packed Z and fog word (Z in the low 24 bits, fog in the top 8) into a vertex. */
static inline void gt4_vertex_zf(struct gt4_vertex *v, unsigned int zf)
{
    v->z = zf & 0xFFFFFF;
    v->fog = (unsigned char)(zf >> 24);
}

#endif
