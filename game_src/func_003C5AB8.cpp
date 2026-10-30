/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 003C5AB8 (from its matched source): its PLZCW (the power of two at or above the photo texture's
   extents) in plain C. */
typedef unsigned int u32;
struct G { char pad[0x38cc4]; int flag; };
extern G *D_00622F4C;
struct Tex { int x[4]; u32 w,h,u,v; };
extern "C" void func_00105728(Tex *);
extern "C" int func_0049FD50(int,float*,float*,int,int,int);
extern "C" void func_004A29A8(int);
extern "C" void func_004A4550(int,int);
extern "C" void RaceEntryCar__update(int);
extern "C" void func_004A5A90(float,float,float,float,float,float);
extern "C" float *func_004A5B70(int);
extern "C" void func_004A7454();
extern "C" void func_004A7844(float,float,float);
extern "C" void func_004A7890(float,float,float);
extern "C" void func_004A79D8(float);
extern "C" void func_004AA1D8(float,float,float,float);
extern "C" void func_004AB040(u32);
/* PLZCW of n-1 (its leading bits equal to the sign bit, less one) & 31, ^ 31: the bit length of n-1 (of ~(n-1)
   when negative); 1 shifted by it is the power of two at or above n. */
static inline u32 pow2(u32 n) { u32 x=n-1; if((int)x<0) x=~x; u32 b=0; while(x) { b++; x>>=1; } return 1U<<b; }
static inline float ratio(u32 n) { float v=(float)n; return v/(float)pow2(n); }
extern "C" void func_003C5AB8(void *, Tex *t, int mode) {
 int flag=D_00622F4C->flag;
 RaceEntryCar__update(1); func_004A7454();
 float width=410.0f;
 func_004A5A90(0.0f,640.0f,480.0f,0.0f,-1.0f,1.0f);
 RaceEntryCar__update(0); func_004A7454();
 float height=308.0f;
 if(mode) { if(flag) { func_004A7844(289.5f,20.0f,0.0f); func_004A7890(0.75f,1.0f,1.0f); } else {func_004A7844(328.0f,20.0f,0.0f);} func_004A79D8(90.0f); }
 else { if(flag) {func_004A7844(72.48f,20.0f,0.0f);func_004A7890(0.75f,1.0f,1.0f);} else {func_004A7844(20.0f,20.0f,0.0f);} }
 func_004AB040(5);func_004AB040(6);func_004AB040(4);
 func_004A29A8(0);func_004A4550(3,1);func_004A4550(2,1);func_00105728(t);
 float *p=func_004A5B70(3); float *q=func_004A5B70(2);
 float x=ratio(t->w+t->u);
 float y=ratio(t->h+t->v);
 p[3]=width;p[7]=height;p[9]=width;p[10]=height;
 p[0]=0;p[1]=0;p[2]=0;p[4]=0;p[5]=0;p[6]=0;p[8]=0;p[11]=0;
 q[2]=x;q[5]=y;q[6]=x;q[7]=y;q[0]=0;q[1]=0;q[3]=0;q[4]=0;
 func_004AA1D8(1.0f,1.0f,1.0f,1.0f);
 func_0049FD50(4,p,q,0,0,0);
}
