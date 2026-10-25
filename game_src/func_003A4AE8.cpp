/* compiler: ee-gcc2.96-no-strict-aliasing */
struct Self { unsigned char p0,fx,fy,p3; short x,y,w,h; float alpha; char p10[16]; unsigned color; int shadow; float width,scale; void *tex; float fade; char p38[28]; float fade2; char p58[24]; char font[32]; char text[1]; };
struct Font { char p[24]; float scale; };
extern Font D_00623838;
extern "C" {
int func_003A4A78(void *) throw();
void func_003AEF18(void *,float,float,unsigned,unsigned);
float func_003AFD18(int, float, float);
unsigned func_003AFEC8(unsigned,float);
void func_0044D920(void *,void *);
void func_0044D988(void *);
void func_0044DBC0(void *,float,float);
void func_0044DDF8(void *,float,float,float);
void func_0044DE80(void *,unsigned);
void GT3OLDFONT__GT3OldFontManagerClass__setShadowValue(float, void *);
float func_0044DFC8(void *,void *);
void GT3OLDFONT__GT3OldFontManagerClass__putstring(void *,void *);
void func_004AA168(unsigned);
}
static inline float minf(float a,float b) { float r; __asm__("min.s %0,%1,%2":"=f"(r):"f"(a),"f"(b)); return r; }
extern "C" void RaceMessageDisplay__render_main(Self *s) {
 if (!func_003A4A78(s)) return;
 float fade=s->fade2;
 float alpha=s->alpha*fade*s->fade;
 if(alpha<=0.0f) return;
 if(s->tex) {
  func_004AA168(func_003AFEC8(s->color,alpha));
  func_003AEF18(s->tex,(float)s->x,(float)s->y,s->fx,s->fy);
 } else {
  func_0044D920(&D_00623838,s->font);
  char *text;
  if(s->width==0.0f) {
   text=s->text;
   float width=func_0044DFC8(&D_00623838,text)/D_00623838.scale;
   s->scale=1.0f; s->width=width;
   if(s->w>0) {
    float scale=minf(1.0f,(float)s->w/width);
    s->scale=scale; s->width=width*scale;
   }
  }
  else { text=s->text; }
  Font *font=&D_00623838;
  func_0044DBC0(font,s->scale,1.0f);
  float shadow=s->shadow==1 ? 0x1.333332p+0f : 0.0f;
  GT3OLDFONT__GT3OldFontManagerClass__setShadowValue(shadow, font);
  unsigned color=func_003AFEC8(s->color,alpha);
  alpha=0.0f;
  func_0044DE80(&D_00623838,color);
  func_0044DDF8(&D_00623838,func_003AFD18(s->fx, (float)s->x, s->width),(float)s->y,alpha);
  GT3OLDFONT__GT3OldFontManagerClass__putstring(&D_00623838,text);
  func_0044DBC0(&D_00623838,1.0f,1.0f);
  GT3OLDFONT__GT3OldFontManagerClass__setShadowValue(alpha, &D_00623838);
  func_0044D988(&D_00623838);
 }
}
/* native correction of src/race/RaceMessageDisplay/RaceMessageDisplay__render_main.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes GT3OLDFONT__GT3OldFontManagerClass__setShadowValue; classes func_003AFD18 */
