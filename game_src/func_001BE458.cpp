/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 001BE458 (from its matched source): its PLZCW (the power of two at or above the camera image's
   width and height) in plain C. */
typedef unsigned int u32;

struct E { short delta, index; void *fn; };   /* vtable entry */
struct V2 { float x, y; };
struct S { char pad[0xA0]; char *fallback, *image; int flip; };

#define F(p, t, o) (*(t *)((char *)(p) + (o)))

extern "C" float mWidget__getWindowW(S *);   /* mWidget::getWindowW */
extern "C" float mWidget__getWindowH(S *);   /* mWidget::getWindowH */
extern "C" int func_004997B0(int, u32, u32, void *);
extern "C" void func_004AB040(int);
extern "C" void func_004A1638(int);
extern "C" void func_004A2808(int, float);
extern "C" void func_004A4810(int, u32, u32);
extern "C" void func_004A4550(int, int);
extern "C" void func_004AA1D8(float, float, float, float);

static inline u32 get(char *o, int off) { E *e = (E *)(F(o, char *, 4) + off); return ((u32 (*)(char *))e->fn)(o + e->delta); }
/* PLZCW of n-1 (its leading bits equal to the sign bit, less one) & 31, ^ 31: the bit length of n-1 (of ~(n-1)
   when negative); 1 shifted by it is the power of two at or above n. */
static inline u32 extent(u32 n) {
    u32 x = n - 1;
    if ((int)x < 0) x = ~x;
    u32 b = 0;
    while (x) { b++; x >>= 1; }
    return 1U << b;
}
static inline bool present(char *p) { return p != 0; }

extern "C" void func_001BE458(S *self, char *ctx) {
    V2 quad[4], uv[4];
    char **image = &self->image;
    if (present(*image)) {
        if (get(*image, 0x1c0)) {
            float zero = 0.0f;
            float w = mWidget__getWindowW(self), h = mWidget__getWindowH(self);
            quad[0].x = zero; quad[0].y = zero;
            quad[1].x = w;    quad[1].y = zero;
            quad[2].x = zero; quad[2].y = h;
            quad[3].x = w;    quad[3].y = h;
            u32 width = get(*image, 0x1c8), height = get(*image, 0x1d0);
            float left = zero, top = zero;
            float right = (float)width / (float)extent(width);
            float bottom = (float)height / (float)extent(height);
            if (self->flip) { left = right; right = 0; }
            V2 *tex = uv;
            tex[0].x = left;  tex[0].y = top;
            tex[1].x = right; tex[1].y = top;
            tex[2].x = left;  tex[2].y = bottom;
            tex[3].x = right; tex[3].y = bottom;
            void *pixels = (void *)get(*image, 0x298);
            func_004997B0(0, width, height, pixels);
            func_004AB040(15); func_004AB040(17); func_004AB040(5); func_004AB040(4);
            func_004A1638(2); func_004AB040(8); func_004A1638(6); func_004A2808(0x44, 1.0f);
            func_004A4810(0, 0, width - 1); func_004A4810(1, 0, height - 1);
            func_004A4550(4, 2); func_004A4550(5, 2); func_004AA1D8(1, 1, 1, 1);
            E *e = (E *)(F(ctx, char *, 4) + 0x1b0);
            ((void (*)(char *, int, V2 *, V2 *, int))e->fn)(ctx + e->delta, 4, quad, tex, 0);
        }
        char *f = self->fallback;
        if (present(f)) {
            E *e = (E *)(F(f, char *, 4) + 0x1a8);
            char *obj = f + e->delta;
            float w = mWidget__getWindowW(self), h = mWidget__getWindowH(self);
            ((void (*)(char *, char *, float, float, int))e->fn)(obj, ctx, w, h, self->flip);
        }
    }
}
