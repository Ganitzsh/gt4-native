/* compiler: ee-gcc2.96-no-strict-aliasing */
#define I(p,o) (*(int*)((char*)(p)+(o)))
#define F(p,o) (*(float*)((char*)(p)+(o)))
#define P(p,o) (*(char**)((char*)(p)+(o)))
struct Player { int data[4]; };
struct Matrix { float data[16]; } __attribute__((aligned(16)));
struct Vec4 { float x,y,z,w; void set(float a,float b,float c,float d) { x=a; y=b; z=c; w=d; } };
struct Context { char *self,*model; Context(char *s,char *m) : self(s),model(m) {} };
struct Callback { char *vt,*model; Callback(char *m); };
extern char D_0065B7B0[],D_0068FDF0[],D_0068FDF8[],D_0068FE00[];
inline Callback::Callback(char *m) { vt=D_0065B7B0; model=m; }
struct Handle { char *p; };
extern "C" {
void func_00154BB8(void*) throw(); void func_00154CA8(void*,int) throw(); int func_00156660(void*) throw();
void func_00157378(void*,int,int); void func_001574E8(void*);
void func_00201F18(void*,void*); void func_002030A0(void*); void *func_002030E8(void*,void*); void func_00203118(void*,int); void func_0021B528(void*,float,float);
void func_00156698(); void func_001567F8(); void func_00156A80();
void CompatibleModel__structor_0(int,int,int,void*,void*,void*,void(*)(),void*,void(*)(),void*,int,void(*)(),void*,float,float);
void func_00427698(void*); void func_00427820(void*); void GT4_Motion__MotionSetPlayer__set(void*,int,void*);
int func_004278B8(void*,int); void func_00427A70(void*,int,float,int,float); int func_00427E18(void*,int); int func_00428298(void*,void*,int,int,float,float); int func_004283D0(void*,int); void func_00428670(void*,int,void*,int,float,float);
int func_00429360(void*,int,void*,float*,float,float); float func_0042A100(void*,int); float func_0042A190(void*,int);
void func_004A53F8(); void func_004A5400(); void func_004A7454(); void func_004A7550(void*);
void mCarModelPS2__virtual_57(char *self,int a1,int a2,int a3,char *a4,char *a5,char *a6,int a7) {
 func_00154BB8(self+0x1b4);
 if(I(self,0x94)) { func_00157378(self,a3,I(a4,4)); return; }
 if (!func_00156660(self) && !I(self,0x90)) { func_001574E8(self); return; }
 I(self,0x8a8)=0;
 Handle *handle=(Handle*)(self+0x88);
 float fade=1;
 if(handle->p) {
  char *motion=P(handle->p,8);
  if(motion) {
   func_00427698(motion);
   char *state=handle->p+0x10;
   Player player;
   func_00427820(&player); GT4_Motion__MotionSetPlayer__set(&player,0,motion);
   float t=func_0042A100(&player,I(state,8));
   func_0021B528(state,t,func_0042A190(&player,I(state,8)));
   func_00429360(&player,I(state,8),D_0068FDF0,&fade,F(state,4),0.0f);
   if(fade>1) fade=1; if(fade<0) fade=0;
   float lift=0;
   if(func_00429360(&player,I(state,8),D_0068FDF8,&lift,F(state,4),0.0f)) {
    if(lift<0) lift=0;
    F(self,0x8a8)=lift;
   }
   float angle;
   if(func_00427E18(&player,I(state,8))) {
    func_00428298(&player,&angle,I(state,8),0,F(state,4),0.0f);
    F(a5,4)=(angle*180.0f)/0x1.921fb4p+1f;
   }
   if(func_004278B8(&player,I(state,8))) {
    func_004A53F8(); func_004A7454(); func_00427A70(&player,I(state,8),F(state,4),0,0.0f);
    Matrix matrix; func_004A7550(&matrix); func_004A5400(); I(a5,0)=2; func_00201F18(a5,&matrix);
   }
   if(func_004283D0(&player,I(state,8))) {
    Callback cb(a6);
    func_00428670(&player,I(state,8),&cb,0,F(state,4),0.0f);
   }
   angle=0;
   if(func_00429360(&player,I(state,8),D_0068FE00,&angle,F(state,4),0.0f)) {
    if(angle<0) angle=0; if(angle>1) angle=1;
    Vec4 v; func_002030A0(&v); v.set(0,0,0,angle);
    func_002030E8(a6+0x58,&v); func_00203118(&v,2);
   }
  }
 }
 Context ctx(self,a6);
 CompatibleModel__structor_0(a1, a2, a3, a4, a5, a6, func_00156698, &ctx, func_001567F8, self, a7, func_00156A80, self, F(self,0x4c), fade);
 func_00154CA8(self+0x164,0);
 if(I(self,0x188)) func_00154CA8(self+0x178,1); else func_00154BB8(self+0x178);
 if(I(self,0x860)) func_00154CA8(self+0x18c,1); else func_00154BB8(self+0x18c);
 func_00154CA8(self+0x1a0,1);
}
}
/* native correction of src/gamedata/mCarModelPS2/mCarModelPS2__virtual_57.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes CompatibleModel__structor_0 */
