/* compiler: ee-gcc2.96-no-strict-aliasing */
struct Display {
 unsigned char pad0, flagsx, flagsy, pad3;
 short x,y,w,h;
 float opacity;
 char pad10[0x10];
 unsigned int color;
 int shadow;
 float width,scale;
 char pad30[4];
 float alpha34;
 char pad38[0x1c];
 float alpha54;
 char pad58[0x18];
 char font[0x20];
 char text[0xc0];
 int back;
 unsigned int backcolor;
};
struct Font { int m0,m4; char pad8[16]; float m18; };
extern "C" Font D_00623838;
extern "C" void func_004A2808(int,float);
extern "C" unsigned int func_003AFEC8(unsigned int,float);
extern "C" void func_004AA168(unsigned int);
extern "C" void func_003AF108(int, float, float, float, float, int, int);
extern "C" void func_0044D920(Font *,void *);
extern "C" float func_0044DFE8(Font *,void *);
extern "C" void func_0044DBC0(Font *,float,float);
extern "C" void GT3OLDFONT__GT3OldFontManagerClass__setShadowValue(float, Font *);
extern "C" void func_0044DE80(Font *,unsigned int);
extern "C" int func_003AFD78(int,int,int);
extern "C" float func_003AFD18(int, float, float);
extern "C" void func_0044DDF8(Font *,float,float,float);
extern "C" void GT3OLDFONT__GT3OldFontManagerClass__putstring(Font *,void *);
extern "C" void func_0044D988(Font *);
static inline float minf(float a,float b) { float r; __asm__("min.s %0, %1, %2" : "=f"(r) : "f"(a), "f"(b)); return r; }
extern "C" void RaceReplayModeDisplay__render_main(Display *self) {
 float alpha = self->alpha54;
 float a = self->opacity * alpha * self->alpha34;
 if (!(a <= 0.0f)) {
   if (self->back) {
     func_004A2808(0x44,1.0f);
     func_004AA168(func_003AFEC8(self->backcolor,a));
     func_003AF108(self->back, (float)self->x, (float)self->y, (float)self->w, (float)self->h, self->flagsx, self->flagsy);
   }
   func_0044D920(&D_00623838,self->font);
   void *text;
   if (self->width == 0.0f) {
     text = self->text;
     float width = func_0044DFE8(&D_00623838,text) / D_00623838.m18;
     self->scale = 1.0f;
     self->width = width;
     if (self->w > 0) {
       float scale = minf(1.0f,(self->w * 0x1.ccccccp-1f)/width);
       self->scale = scale;
       self->width = width * scale;
     }
   }
   else { text = self->text; }
   Font *font = &D_00623838;
   func_0044DBC0(font,self->scale,1.0f);
   float shadow = self->shadow == 1 ? 0x1.333332p+0f : 0.0f;
   GT3OLDFONT__GT3OldFontManagerClass__setShadowValue(shadow, font);
   unsigned int color = func_003AFEC8(self->color,a);
   a = 0.0f;
   func_0044DE80(&D_00623838,color);
   D_00623838.m4 = 0;
   int x = func_003AFD78(self->x,self->w,self->flagsx) + (self->w >> 1);
   int y = func_003AFD78(self->y,self->h,self->flagsy);
   func_0044DDF8(&D_00623838,func_003AFD18(2, (float)x, self->width),(float)y + 13.0f,a);
   GT3OLDFONT__GT3OldFontManagerClass__putstring(&D_00623838,text);
   func_0044DBC0(&D_00623838,1.0f,1.0f);
   GT3OLDFONT__GT3OldFontManagerClass__setShadowValue(a, &D_00623838);
   func_0044D988(&D_00623838);
 }
}
/* native correction of src/race/RaceReplayModeDisplay/RaceReplayModeDisplay__render_main.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes GT3OLDFONT__GT3OldFontManagerClass__setShadowValue; classes func_003AF108; classes func_003AFD18 */
