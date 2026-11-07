/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native copy of 003BE3C8 (RaceRoundMeterBase::virtual_15, the HUD's round meter, drawn after render_world returns)
 * from gt4-decomp's src/race/RaceRoundMeterBase/RaceRoundMeterBase__virtual_15.cpp, the source the native game compiled
 * until now, with the meter's scene records (docs/modern/CONTRACTS.md a.5.1): a kViewHud view around what it draws (R1:
 * its needle is a 3D model under an orthographic projection, 2 instances a race frame), in the frame the kick closes.
 * No camera (no kViewCameraKnown); the HUD view is never substituted. Nothing is sent while scene records are off. */
#include "gt4_gfx.h"
#include "spr.h"
struct State { unsigned long words[5]; };
struct Texture { char pad[0x18]; State *state; };
struct Entry { short delta,index; void (*fn)(void *); };
struct Self {
    char b0; unsigned char b1,b2; char b3;
    short x,y,w,h; float alpha; Texture *texture; Entry *vt;
    char pad18[0x20]; float xoffset,yoffset,xscale,yscale;
};
extern "C" {
void func_004A7550(void *);
void func_004A53F8();
void func_004A5400();
void func_004A7454();
void func_004A74B4(void *);
int func_003AFD78(int,int,int);
void func_004A7844(float,float,float);
void func_004A7890(float,float,float);
void func_004AA1D8(float,float,float,float);
void func_003AF108(void *, float, float, float, float, unsigned char, unsigned char);
}
static inline void vcall(Self *s,int i) {
    Entry *e=s->vt+i;
    e->fn((char*)s+e->delta);
}
extern "C" void RaceRoundMeterBase__virtual_15(Self *s) {
    float matrix[16];
    if (s->texture) {
        gt4_scene_view_begin(spr_base(), 7, 0, 0);   /* kViewHud */
        func_004A7550(matrix);
        func_004A53F8();
        func_004A7454();
        float sx=(float)s->w;
        float sy=(float)s->h;
        unsigned long bits=s->texture->state->words[4];
        int tw=(bits>>14 & 1023)+1;
        int th=(bits>>34 & 1023)+1;
        sx/=(float)tw;
        sy/=(float)th;
        float x=(float)(func_003AFD78(s->x,s->w,s->b1)+s->w/2)+s->xoffset*sx;
        float y=(float)(func_003AFD78(s->y,s->h,s->b2)+s->h/2)+s->yoffset*sy;
        func_004A7844(x,y,0.0f);
        func_004A7890(s->xscale*sx,s->yscale*sy,1.0f);
        vcall(s,14);
        func_004A53F8();
        func_004A74B4(matrix);
        func_004AA1D8(0x1.333332p+0f,0x1.333332p+0f,0x1.333332p+0f,s->alpha);
        func_003AF108(s->texture, (float)s->x, (float)s->y, (float)s->w, (float)s->h, s->b1, s->b2);
        func_004A5400();
        vcall(s,15);
        func_004A5400();
        gt4_scene_view_end(spr_base());
    }
}
