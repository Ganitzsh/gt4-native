/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native copy of mRenderContextPS2 virtual 61 (00215F78, matched): sets the 2D clip (the GS scissor) to the
 * widget's rectangle, in the 640 x 480 menu layout. With the menus in a centred 4:3 area of the wide mode
 * (game_src/func_00214FB0.cpp) the layout reaches the screen through that projection, so the rectangle is
 * mapped the same way; unmapped it sat right of the text it clips on the right half of the screen (the
 * licence tests' Information box lost the first letter of every line). A clip over the layout's whole
 * width keeps the whole frame, as full-width backgrounds reach its edges (game_src/func_00215CD0.cpp). */
struct Vec4 { float x, y, z, w; };
struct Entry { short delta, index; void (*fn)(void *, Vec4 *, int); };
struct mRenderContextPS2Layout { int first; Entry *vtbl; };

extern "C" void func_004A1638(int);
extern "C" void func_004A2DF8(float, float, float, float);
extern "C" int gt4_display_aspect_milli(void);
extern "C" int gt4_menus_centered(void);
extern int D_006184F0;

extern "C" void mRenderContextPS2__virtual_61(mRenderContextPS2Layout *self, int arg) {
    Vec4 v;
    float zero = 0.0f;
    v.x = zero;
    v.y = zero;
    v.z = zero;
    v.w = zero;
    Entry *e = &self->vtbl[63];
    e->fn((char *)self + e->delta, &v, arg);
    func_004A1638(0x12);
    const int aspect = gt4_display_aspect_milli();
    if (D_006184F0 && aspect >= 1500 && gt4_menus_centered() && !(v.x <= 0.0f && v.x + v.z >= 640.0f)) {
        const float half = 240.0f * (float)aspect / 1000.0f;   /* the projection spans 320 -/+ half */
        const float scale = 320.0f / half;
        v.x = (v.x - (320.0f - half)) * scale;
        v.z = v.z * scale;
    }
    func_004A2DF8(v.x, v.y, v.z, v.w);
}
