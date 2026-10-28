/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native copy of 002472B8 (matched): sets the font manager up for one text face, its box being the current clip
 * rectangle truncated to whole pixels. The rectangle's edges are layout positions the EE computes as whole numbers;
 * the host's float arithmetic leaves some a few ulps below (127.999992 for 128), which truncated to one pixel less
 * made the text one unit wider than its box: centred labels started half a unit to the left and lost their first
 * glyph (the GT mode map's "icense Center"). Edges within 1/1000 of a whole number are taken as that number. */
struct Face {
 float color[4]; char *text; int i14,i18,i1c,i20,i24,i28;
 float f2c,f30,f34,f38,f3c,shadow[4]; int i50,i54,i58; float f5c,f60;
};
struct Renderer {
 char pad[0x24]; float f24,f28; int i2c; char pad30[0x14];
 unsigned int i44,i48; int i4c; char pad50[0x14]; int i64,i68,i6c; float f70; int i74;
};
extern Renderer *PDISTD__global_font_manager;
extern char D_006991B0[];
extern "C" float func_002317C8(void *);
extern "C" void func_00490290(Renderer *, const char *);
extern "C" unsigned int func_00202EA8(float *, float);
extern "C" float func_002313E0(void *);
extern "C" float func_00231400(void *);
extern "C" float func_00231420(void *);
extern "C" float func_00231440(void *);
extern "C" void func_00490700(Renderer *, int,int,int,int);
extern "C" int func_00490740(Renderer *, unsigned int);
extern "C" void func_004908C0(Renderer *, float,float);
static inline const char *text(Face *o) {
 char **s = &o->text;
 char *p = *s;
 int n = *(int *)(p-16);
 const char *r = D_006991B0;
 if (n != 0) { p[n] = 0; r = *s; }
 return r;
}
static inline int whole(float v) {
 int n = (int)(v + (v >= 0.0f ? 0.5f : -0.5f));
 float d = v - (float)n;
 if (d < 0.001f && d > -0.001f) return n;
 return (int)v;
}
extern "C" void func_002472B8(Face *o, void *ctx) {
 Renderer *r = PDISTD__global_font_manager;
 float scale = func_002317C8(ctx);
 func_00490290(r, text(o));
 float sx = o->f2c;
 float sy = o->f30;
 r->f24 = sx;
 r->f28 = sy;
 r->f70 = o->f3c;
 r->i48 = func_00202EA8(o->shadow, scale);
 r->i6c = o->i14;
 r->i68 = o->i18;
 r->i44 = func_00202EA8(o->color, scale);
 r->i2c = o->i28;
 r->i74 = o->i50;
 float x = func_002313E0(ctx);
 float y = func_00231400(ctx);
 float z = func_00231420(ctx);
 float w = func_00231440(ctx);
 func_00490700(r, whole(x), whole(y), whole(z), whole(w));
 if (o->i20 != 0) {
  int v = func_00490740(r,o->i24);
  r->i64 = v;
  r->i4c = 1;
 } else {
  func_004908C0(r,o->f5c,o->f60);
 }
}
