/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native copy of RacePS2Base::render_back_mirror (00341E50, matched): the rear-view mirror. With the host's
 * effects.mirror = high (gt4_game_option(1) = 1) it draws the course from the mirror's own vision list with the
 * main view's mode (+0xF30, where the mirror's +0xF38 leaves out the course's detail objects), without
 * forcing the lowest texture level, and the cars at the detail their distance gives (as the main view)
 * instead of level 2. Original otherwise. (The main view's list, course + 8, holds only what is ahead: the
 * mirror lost the stands and gantries behind the car with it.) */
typedef int s32;
struct V { short d,i; void *fn; };
struct Buf { int v[15]; };
struct Global { Buf b[2]; int cur; };
extern Global D_0061850C;
#define P(p,o) (*(char **)((char *)(p)+(o)))
#define I(p,o) (*(int *)((char *)(p)+(o)))
#define F(p,o) (*(float *)((char *)(p)+(o)))
typedef char *(*Get)(char *,int);
typedef void (*Act)(char *,float);
typedef void (*Render)(char *,void *,int,int,float,int);
extern "C" {
void func_00105280(void *,int);
int func_001052E0(void *,int,int,int);
void *func_00105378(void *,void *);
void func_001054D8(void *,int,int,int,int);
void func_001056A0(void *);
int func_001058A0(void *);
float func_001058E8(void *);
void func_00342580(void *,float,float);
void func_00396B80(void *,void *,int,float,float);
void CourseData__render_course(void *,void *,void *,int);
void func_00397CE0(void *);
void func_00397D48(void *);
void func_00397DA0(void *,float);
void func_00397DF8(void *);
int ModelSet2__texLOD(int);
int ModelSet2__texLevel(int);
void func_00456918(float);
void func_004A1638(int);
void pglFrontFace(int);
void func_004A3040(float,float,float,float);
void func_004A3078(int);
void func_004A3088(float);
void func_004A3230(int);
void RaceEntryCar__update(int);
void func_004A53F8();
void func_004A5400();
void pglPushAttrib(int);
void func_004A5688();
void func_004A6AF8(int,int,int,int);
void func_004A7454();
void func_004A7890(float,float,float);
void func_004AA168(int);
void func_004AB040(int);
int gt4_game_option(int id); /* runtime/sdk.cpp: the host's options */
}
extern "C" void RacePS2Base__render_back_mirror(char *self,void *out,int flag) {
 const int high=gt4_game_option(1)==1;
 Buf b,aux;
 func_00105378(&b,out);
 func_001054D8(&b,(int)F(self,0xE2E0),(int)F(self,0xE2E4),(int)F(self,0xE2E8),(int)F(self,0xE2EC));
 char *course=P(P(P(self,0x6C),0x80),4);
 char *scene=P(course,0x80);
 V *e=(V *)(P(self,0x64)+0xD0);
 char *cam=((Get)e->fn)(self+e->d,0);
 pglPushAttrib(0x8C7);
 pglFrontFace(1);
 RaceEntryCar__update(1); func_004A53F8();
 RaceEntryCar__update(0); func_004A53F8();
 func_001052E0(&aux,2,64,64);
 func_001056A0(&aux);
 func_004A3078(0x80000000);
 func_004A3230(1);
 func_001056A0(&b);
 RaceEntryCar__update(1); func_004A7454();
 float f=60.0f;
 if(flag==0) f=120.0f;
 F(cam,0xA08)=f;
 V *e10=(V *)(P(cam,0)+0x10);
 ((Act)e10->fn)(cam+e10->d,0.0f);
 float scale=F(self,0xE2F0);
 float ratio=func_001058E8(&b);
 ratio/=func_001058E8(&D_0061850C.b[D_0061850C.cur]);
 func_004A7890(ratio*scale,scale,1.0f);
 func_00456918(1.0f);
 RaceEntryCar__update(0); func_004A7454();
 func_004A7890(1.0f,1.0f,-1.0f);
 V *e18=(V *)(P(cam,0)+0x18);
 ((Act)e18->fn)(cam+e18->d,0.0f);
 func_00397CE0(scene);
 func_00397DA0(scene+0xA90,1.0f);
 func_00397DF8(scene);
 func_004A3040(0.0f,0.0f,0.0f,0.0f);
 func_004A3230(3);
 func_00396B80(course,P(course,0x60),-1,1.0f,1000.0f);
 func_004A3230(2);
 if(high) {
  /* the mirror's own vision list (the main view's, course + 8, holds only what is ahead), with the main view's
     mode (its bit 0 adds the course's detail objects) and textures at their own level */
  if(flag && P(course,0x1C)) CourseData__render_course(course,P(course,0x1C),cam,I(P(course,0x80),0xF30));
  else CourseData__render_course(course,P(course,0xC),cam,I(P(course,0x80),0xF30));
 } else {
  ModelSet2__texLOD(1); ModelSet2__texLevel(1);
  if(flag && P(course,0x1C)) CourseData__render_course(course,P(course,0x1C),cam,I(P(course,0x80),0xF38));
  else CourseData__render_course(course,P(course,0xC),cam,I(P(course,0x80),0xF38));
  ModelSet2__texLOD(0);
 }
 func_00397D48(scene);
 func_00342580(&b,F(scene,0x3C),1.0f);
 func_004A1638(5);
 V *er=(V *)(P(self,0x64)+0x380);
 /* the cars: at the detail level the mirror forces (2), or chosen by distance as in the main view (-1) */
 ((Render)er->fn)(self+er->d,&aux,high?-1:2,0,0.0f,-1);
 func_004AB040(5);func_004AB040(4);func_004AB040(6);
 int width=b.v[6];
 int height=func_001058A0(&b)-1;
 func_004AA168(0);
 width--;
 func_004A6AF8(0,0,0,height);
 func_004A6AF8(0,height,width,height);
 func_004A6AF8(width,height,width,0);
 func_004A6AF8(width,0,0,0);
 func_004A1638(5); func_004A3088(0.0f);
 func_004A3230(2); func_004A3088(1.0f);
 RaceEntryCar__update(1); func_004A5400();
 RaceEntryCar__update(0); func_004A5400();
 pglFrontFace(0); func_004A5688();
 func_00397DA0(scene+0x60,1.0f);
 func_00105280(&aux,2); func_00105280(&b,2);
}
