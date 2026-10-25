/* compiler: ee-gcc2.96-no-strict-aliasing */
struct Image { char pad[0x20]; unsigned long size; };
struct Tex { char pad[0x18]; Image *image; };
struct Meter { char pad[0x18]; Tex *tex,*accel,*brake,*asmtex,*tcs,*grip; float a,b,c,d,e; unsigned color; signed char mode1,mode2; char pad4a[2]; float osc; char pad50[0x1c]; int flags; };
struct Buffer { int id,field4; };
extern "C" {
void func_00105460(Buffer *,int,int);
int func_00105528(Buffer *);
void func_001056A0(Buffer *);
void Oscillator__setCount(void *,int,unsigned char) throw();
void RaceMTRMeter__init_texset(Meter *);
void func_003AC140(Meter *, Tex *, int, float, float, float, float, float, float);
void func_003AF108(Tex *,float,float,float,float,int,int);
void func_003AF2F0(Tex *,float,float,unsigned,unsigned,int,int);
void func_003AF4F8(Tex *,float,float,float,float,int,int);
unsigned RaceDisplayUtil__getScaledColor(unsigned,float,float);
int func_00499728(Tex *);
void func_0049CC48(Image *);
void func_004A1638(int);
void func_004A2750(int,float);
void func_004A27C8(int);
void func_004A2808(int,float);
void func_004A2980(int);
void func_004A29A8(int);
void func_004A3078(int);
void func_004A3230(int);
void RaceEntryCar__update(int);
void func_004A53F8();
void func_004A5400();
void pglPushAttrib(int);
void func_004A5688();
void func_004A5A90(float,float,float,float,float,float);
void func_004A7454();
void func_004AA168(unsigned);
void func_004AB040(int);
}
static inline float mn(float a,float b) { float r; __asm__("min.s %0, %1, %2":"=f"(r):"f"(a),"f"(b)); return r; }
static inline float mx(float a,float b) { float r; __asm__("max.s %0, %1, %2":"=f"(r):"f"(a),"f"(b)); return r; }
static inline float oscValue(float *p) { return *p; }
extern "C" void RaceMTRMeter__render_buffer(Meter *m,Buffer *buf) {
 pglPushAttrib(1); RaceMTRMeter__init_texset(m);
 unsigned long bits=m->tex->image->size;
 int w=((bits>>14)&1023)+1;
 int h=((bits>>34)&1023)+1;
 if(buf->id==-1) { func_00105460(buf,w,h); buf->field4=0; func_00105528(buf); }
 func_001056A0(buf); func_004A2980(-1); func_004A3078(0); func_004A3230(1);
 float fw=w;
 func_004AA168(0x80ffffff); func_004A1638(6); func_004AB040(15); func_004AB040(4); func_004AB040(5);
 float fh=h;
 func_004A29A8(0); func_004A2808(0x44,0.0f);
 float cx=fw*0.5f, cy=fh*0.5f;
 RaceEntryCar__update(1); func_004A53F8(); func_004A7454(); func_004A5A90(0,fw,fh,0,-1,1);
 RaceEntryCar__update(0); func_004A53F8(); func_004A7454(); func_003AF4F8(m->tex,0,0,fw,fh,0,0); func_004A2980(0xffffff);
 float base,end1,end2;
 
 if((m->flags&1)==0) {base=0;end1=m->b*60;end2=m->a*60;} else {base=180;end1=180-m->b*60;end2=180-m->a*60;}
 func_004AA168(0x80808080); func_003AC140(m, m->accel, 16, cx, cy, 45.25f, 65, base, end2);
 func_004AA168(0x80ffffff); func_003AC140(m, m->accel, 16, cx, cy, 45.25f, 65, base, end1);
 
 if((m->flags&1)==0) {end1=mn(m->c,m->d)*-60;end2=mx(m->c,m->d)*-60;base=0;} else {base=180;end1=mn(m->c,m->d)*60+base;end2=mx(m->c,m->d)*60+base;}
 func_004AA168(0x80808080); func_003AC140(m, m->brake, 16, cx, cy, 23.5f, 42.5f, base, end2);
 func_004AA168(0x80ffffff); func_003AC140(m, m->brake, 16, cx, cy, 23.5f, 42.5f, base, end1);
 func_004AA168(m->color|0x80000000); func_00499728(m->grip); func_0049CC48(m->grip->image);
 float r=m->e*46; func_003AF108(m->grip,cx,cy,r,r,2,2);
 func_004A2808(0x44,0); func_004A1638(4); func_004A2750(6,0.5f); func_004A27C8(0); func_004A2980(-1);
 float *osc=&m->osc;
 float value=oscValue(osc);
 float values[3]; values[0]=0; values[1]=1; values[2]=value;
 if(m->mode1==2||m->mode2==2) Oscillator__setCount(osc,1,0);
 func_004AA168(RaceDisplayUtil__getScaledColor(0x800099ff,values[m->mode1],1)); func_003AF2F0(m->asmtex,0,fh,0,1,0,0);
 func_004AA168(RaceDisplayUtil__getScaledColor(0x800000c8,values[m->mode2],1)); func_003AF2F0(m->tcs,0,0,0,0,0,0);
 func_004AB040(4); func_004A5400(); RaceEntryCar__update(1); func_004A5400(); RaceEntryCar__update(0); func_004A5688();
}
/* native correction of src/race/unit_003ABC10/RaceMTRMeter__render_buffer.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_003AC140 */
