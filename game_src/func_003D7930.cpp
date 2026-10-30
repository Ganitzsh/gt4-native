/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 003D7930 (from its draft, checked against the original): its PLZCW (the power of two at or above
   the map target's extents) in plain C; the names it compares are the strings at 006A3300..006A3330. */
typedef unsigned int u32;
struct SectorArray {char pad[0xdc];unsigned char v[4];};
struct Rect {float x,y,z,w; const char *name;};
struct Str {char *p; unsigned size()const{return ((unsigned*)p)[-4];} int at(unsigned i)const{if(i==size())return 0;return p[i];} int last()const{return at(size()-1);} };
struct Target {
 int f0,f4,f8,fc; unsigned x,y,w,h; char pad[32];
 Target() __asm__("func_00105250");
 Target(int,int,int) __asm__("func_001052E0");
 ~Target() __asm__("func_00105280");
 void set(int) __asm__("func_001053F8");
 void size(int,int) __asm__("func_00105460");
 void format(int) __asm__("func_001054A0");
 void clip(int,int,int,int) __asm__("func_001054D8");
 void bind() __asm__("func_001056A0");
 void finish() __asm__("func_00105728");
};
struct View {char pad[40];int format;char tail[16];};
extern struct Global {View v[2];int cur;} D_0061850C;
struct VEntry {short delta,idx; int (*fn)(void*);};
struct Course {char pad[0x10140]; VEntry *vt;};
struct Display {char pad[12];float alpha;char p[16];float x,y;};
struct Entry {char pad[24];void *state;};
extern "C" {
 int func_0057F238(const char*,const char*) throw();
 void func_0042DC90(void*,void*); void func_0042E160(void*);
 void func_004A1638(int);void func_004A2808(int,float);
 void func_004AA168(unsigned);void func_003AFAE0(float,float,float,float);
 void func_00393790(void*,int,float,float,float,float,float);
 void func_004A5418(int);void func_004A5688();unsigned func_004A7C28();
 void func_004A7CA0(int*,int*,int*,int*);void func_004A7CE0(int*,int*,int*,int*);
 void func_003BF268(void*,Target*,void*,int);
 void func_003AF6E0(float,float,float,float,float,float,float,float);
 void func_003AEC80(Display*,int,int);Entry *func_003DA0C0(void*);
 void func_003D80A8(void*,void*,void*); int func_00354E38(void*);
 float func_00354E60(void*);float func_00354E58(void*);
 void func_003A5FD8(Display*,Target*);void func_003A57A8(Display*,Target*);
}
#define FIELD(p,t,o) (*(t*)((char*)(p)+(o)))
/* PLZCW of n-1 (its leading bits equal to the sign bit, less one) & 31, ^ 31: the bit length of n-1 (of ~(n-1)
   when negative); 1 shifted by it is the power of two at or above n. */
inline unsigned power(unsigned n){unsigned x=n-1;if((int)x<0)x=~x;unsigned b=0;while(x){b++;x>>=1;}return 1U<<b;}

extern "C" void func_003D7930(char *self,int unused,Str *str,Rect *r){
 const char **name=&r->name;
 if(func_0057F238("chip",*name)==0){
  int index=str->last();
  char *out=self+0x12b4;
  func_0042DC90(out,self+((int*)self)[index]*16+0x12e0);
  float x=r->x,y=r->y,z=r->z,w=r->w;
  FIELD(out,float,28)=x;FIELD(out,float,32)=y;FIELD(out,float,36)=z;FIELD(out,float,40)=w;
  func_0042E160(out);func_004A1638(6);func_004A2808(0x44,0);
 }else if(func_0057F238("sector",*name)==0){
  int index=((int*)self)[str->last()];int i=0;
  Course *c=FIELD(self,Course*,16);VEntry *ve=c->vt+42;int n=ve->fn((char*)c+ve->delta);
  float x=r->x, width=r->z-x, top=r->y,bottom=r->w;
  if(n<=0)return;
  float step=width/(float)n; unsigned char *sector=(unsigned char*)(index+(int)self+0xdc);
  unsigned *colors=(unsigned*)(self+index*64+0x118);
  for(;i<n;i++){
   float left=x+width*(float)i/(float)n;
   func_004AA168(colors[i]);
   float right=left+step;
   func_003AFAE0(left,top,right,bottom);
   if(i==*sector){func_004AA168(0x80c8c8c8);func_003AFAE0(left,top+(bottom-top)*0.8f,right,bottom);}
  }
 }else if(func_0057F238("cargap",*name)==0){
  func_00393790(FIELD(self,char*,20)+((int*)(((int*)self)+str->last()))[-22]*8,1,r->x,r->y,r->z,r->w,1);
 }else if(func_0057F238("current_car",*name)==0){
  func_00393790(FIELD(self,char*,20)+FIELD(self,int,100)*8,0,r->x,r->y,r->z,r->w,1);
 }else if(func_0057F238("map",*name)==0){
  func_004A5418(0x81);
  int a,b,c,d,e,f,g,h;
  func_004A7CA0(&a,&b,&c,&d);func_004A7CE0(&e,&f,&g,&h);
  Target old;old.set(a);old.f4=b;old.size(c,d);old.format(D_0061850C.v[D_0061850C.cur].format);old.clip(e,f,g,h);
  Target tmp(0,(int)(r->z-r->x),(int)(r->w-r->y));
  func_003BF268(self+0x298,&tmp,FIELD(self,void*,12),FIELD(self,int,100));
  old.bind();tmp.finish();func_004AA168(0x80ffffff);
  unsigned w=tmp.x+tmp.w;float u=(float)w/(float)power(w);
  unsigned h2=tmp.y+tmp.h;float v=(float)h2/(float)power(h2);
  func_003AF6E0(r->x,r->y,r->z,r->w,0,0,u,v);func_004A5688();
 }else if(func_0057F238("tire",*name)==0){
  func_004A5418(0x81);float alpha=(float)((func_004A7C28()>>24)&255)/128.0f;
  int x=(int)r->x,y=(int)r->y;
  Display *tire=(Display*)(self+0x328),*fuel=(Display*)(self+0x300);
  func_003AEC80(tire,x+12,y+10);tire->alpha=alpha;
  void *state=func_003DA0C0(self)->state;func_003D80A8(self,tire,state);
  func_003AEC80(fuel,x+60,y+44);int active=func_00354E38(state);
  if(!active)alpha*=0.25f;fuel->alpha=alpha;
  if(active){fuel->x=func_00354E60(state);fuel->y=func_00354E58(state);}
  int a,b,c,d,e,f,g,h;
  func_004A7CA0(&a,&b,&c,&d);func_004A7CE0(&e,&f,&g,&h);
  Target old;old.set(a);old.f4=b;old.size(c,d);old.format(D_0061850C.v[D_0061850C.cur].format);old.clip(e,f,g,h);
  func_003A5FD8(tire,&old);func_003A57A8(fuel,&old);func_004A5688();
 }
}
