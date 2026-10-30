/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0049B850 (provisional draft drafts/0049b850, not matched): as 0049B540 for points with a
 * depth (pos: x, y, z; Z = (1 - z) x 0xFFFFFF clamped, the fog byte of +0x248): a Gouraud triangle fan of n
 * points with texel coordinates (uv) and per-point colours when given, else the sprite colour (+0xC24, textured)
 * or the fill colour (+0xC20). The original wrote a DIRECT packet; here the same values are a draw record
 * (compat/game/gt4_gfx.h). */
#include "spr.h"
#include "gt4_gfx.h"
typedef unsigned int u32;
typedef unsigned long u64;
struct Spr {
 char pad0[7]; unsigned char mode;
 char pad8[0x42]; unsigned short prim;
 char pad4c[0x1ac]; u64 *cursor;
 char pad1fc[0x4c]; u32 fog;
 char pad24c[0x20]; unsigned short ox, oy;
 char pad270[0x18]; int x, y;
 char pad290[0x990]; u32 rgba, rgbaTex;
};
extern "C" void func_0049FE28();
extern "C" void func_0049B850(int n, float *pos, float *uv, u32 *color) {
 Spr *s = (Spr *)spr_base();
 float scale = s->mode ? 8.0f : 16.0f;
 func_0049FE28();
 u32 prim = (s->prim | 13) & 0xffff;
 if (uv) prim |= 0x110;
 float x = (float)s->x * 16.0f;
 float y = (float)s->y * 16.0f;
 u32 fog = s->fog & 0xff000000;
 unsigned short ox = s->ox, oy = s->oy;
 const u32 fixed = uv ? s->rgbaTex : s->rgba;
 struct gt4_vertex *verts = (struct gt4_vertex *)__builtin_alloca((n > 0 ? n : 1) * sizeof(struct gt4_vertex));
 for (int i = 0; i < n; i++) {
  struct gt4_vertex *o = &verts[i];
  o->u = o->v = 0;
  if (uv) {
   float v = uv[1], u = uv[0];
   uv += 2;
   o->u = (unsigned short)(int)(u * 16.0f);
   o->v = (unsigned short)(int)(v * 16.0f);
  }
  o->rgba = color ? *color++ : fixed;
  o->s = o->t = 0.0f;
  o->q = 1.0f;
  const u32 w = ((ox + (int)(x + pos[0] * 16.0f)) & 0xffff) | ((oy + (int)(y + pos[1] * scale)) << 16);
  o->x = (unsigned short)w;
  o->y = (unsigned short)(w >> 16);
  float z = pos[2];
  pos += 3;
  int d = (int)((1.0f - z) * 16777215.0f);
  if (d < 0) d = 0;
  if (d > 0xffffff) d = 0xffffff;
  gt4_vertex_zf(o, (u32)d | fog);
  o->kick = i >= 2;
  o->pad = 0;
 }
 gt4_gfx_draw(s, prim, GT4_DRAW_PRIM | GT4_DRAW_FOG | GT4_DRAW_COLOR | (uv ? GT4_DRAW_UV : 0), verts, n);
}
