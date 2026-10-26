/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native correction of the provisional draft build/swarm/v4/003ceb90_best.cpp (argument order, argcheck.py orders): Pitmen__initCamera (003CD188) takes its float last, after six ints, as its draft defines it; the draft's order here is the same EE call but other stack slots natively. */
#define F(data,t,o) (*(t *)((char *)(data)+(o)))
// Provisional position layout; crew-detail distance is measured in the ground plane.
struct Position3 { float x,y,z; };
struct PlanarOffset { float x,z; void setDifference(const Position3 &a,const Position3 &b) { x=a.x-b.x; z=a.z-b.z; } float squaredLength() const {return x*x+z*z;} };
struct AnimationIntervals { float start[4],end[4],inverseDuration[4]; };
// Query result layout is provisional; only the time at offset 4 is consumed here.
struct AnimationQuery { int unknown0; float time,unknown8; int unknown12,unknown16,unknown20; };
// Provisional polymorphic object: the vptr follows its two data words.
struct RenderSource {
 int unknown0, unknown4;
 virtual void slot0(); virtual void slot1(); virtual void slot2();
 virtual void slot3(); virtual void slot4(); virtual void slot5();
 virtual void slot6(); virtual void slot7(); virtual void slot8();
 virtual char *renderObject();
};
extern "C" {
char *func_0034C190(char *,int);
short func_0034C750(char *);
int func_00396800(char *);
int func_003C9F38(char *) throw();
void func_003CB070(char *,int,char *,int) throw();
int func_003CC7B0(char *) throw();
char *func_003CC808(char *) throw();
int func_003CC8E8(char *) throw();
void func_003CCA38(char *) throw();
void func_003CCAE0(char *) throw();
void func_003CCC28(char *,int,char *,char *) throw();
void func_003CD188(char *,int,char *,int,int,int,float) throw();
void func_003CD430(char *,int,char *) throw();
void func_003CD730(char *,int,char *) throw();
void func_003CD920(char *,char *) throw();
void func_003CDB20(char *,int,char *) throw();
int func_003CE178(char *,int,char *) throw();
int func_003CE288(char *,int,char *,int *,int *) throw();
void func_003CE930(char *,int,char *) throw();
void func_003CF198(char *,int);
void func_003D5528(char *,int,int,char *);
void func_003D55C0(char *,int,int,char *,int);
float func_003F3708(char *);
float func_003F3738(char *);
int func_00429F98(AnimationQuery *,char *,int,char *);
extern char D_006A3058[], D_006A3070[];
extern const float D_006A3060[], D_006A3078[];
}
static inline int usesPrimaryWeight(char *data,int carIndex) { return ((int *)(data+0x9684))[carIndex]<4; }
static inline void setAnimationInterval(float *data,float a,float b) { data[0]=a; data[4]=b; data[8]=1.0f/(b-a); }
// Provisional entity layout: a four-component placement vector begins at offset 4.
static inline void setPlacementVector(char *data,float x,float y,float z,float channelWeight) { F(data,float,4)=x; F(data,float,8)=y; F(data,float,12)=z; F(data,float,16)=channelWeight; }
static inline char *getRenderObject(char *data) { return ((RenderSource *)data)->renderObject(); }
extern "C" void func_003CEB90(char *self,int updateMode,char *owner,char *context) {
 PlanarOffset offset; int firstFlag,secondFlag; AnimationQuery out;
 int channelIndex;
 func_003CF198(self,updateMode);
 if(!func_003CC7B0(self)) return;
 if(!func_0034C750(owner)) return;
 func_003CCAE0(self);
 func_003CCA38(self);
 int carCount=F(owner,int,0xF8A0);
 int carIndex=0;
 if(carCount>0) {
 char *stateCursor=self+0x9684;
 char *previousStatesBase=self+12;
 char *activeCursor=self+0x9290;
 char *metricsCursor=self+0x789C;
 char *weightsBase=self;
 for(carIndex=0;carIndex<carCount;activeCursor+=4,metricsCursor+=0xA0,weightsBase+=16,stateCursor+=4,carIndex++) {
  char *car=func_0034C190(owner,carIndex);
  char *cars=F(F(owner,char *,0),char *,0x60);
  char *group=self+carIndex*0x8D0+0x3E4;
  char *other=F(F(cars,char *,8),char *,carIndex*4);
  func_003CB070(metricsCursor,carIndex,owner,func_00396800(F(context,char *,4)));
  func_003CD730(self,carIndex,car);
  func_003CDB20(self,carIndex,owner);
  func_003CE178(self,carIndex,car);
  firstFlag=1; secondFlag=1;
  if(func_003CE288(self,carIndex,owner,&firstFlag,&secondFlag)) {
   float f=0.0f;
   char *data=F(getRenderObject(F(other,char *,0x2880)),char *,0x50);
   if(data) f=F(data,float,0xB4);
   func_003CE930(self,carIndex,owner);
   func_003CD188(self,carIndex,car,F(self+carIndex*4,int,0x9658),firstFlag,secondFlag,f);
  } else {
   func_003CE930(self,carIndex,owner);
   func_003CD188(self,carIndex,car,0,firstFlag,secondFlag,0.0f);
  }
  float f=func_003F3738(car)*0.5f;
  if(f<1.708f) f=1.708f;
  if(!F(F(self,char *,0),int,8)) f=-f;
  channelIndex=4;
  setPlacementVector(group+0x758,f,0.0f,-1.0f-func_003F3708(car),0.0f);
  int mode=func_003C9F38(car);
  F(previousStatesBase+carIndex*4,int,0x9690)=mode;
  F(stateCursor,int,0)=mode;
  func_003CD430(self,carIndex,owner);
  if(F(stateCursor,int,0)) F(activeCursor,int,0)=1; else F(activeCursor,int,0)=0;
  char *channelWeight=weightsBase+0x9580;
  for(;channelIndex!=0;channelIndex--,channelWeight+=4) {
   if(usesPrimaryWeight(self,carIndex)) {
    F(channelWeight,float,-0x60)=1.0f;
    F(channelWeight,float,0)=0.0f;
   } else {
    F(channelWeight,float,-0x60)=0.0f;
    F(channelWeight,float,0)=1.0f;
   }
  }
 }
 }
 int type=F(F(self,char *,0),int,8);
 char *tiny=self+0x7C5C;
 func_003D5528(tiny,type,func_003CC8E8(self),self);
 for(carIndex=0;carIndex<carCount;carIndex++) {
  int detailLevel=0;
  if(carIndex>0) {
   const Position3 &current=F(self+carIndex*0xA0,Position3,0x78A8);
   const Position3 &precedingCar=F(self+carIndex*0xA0,Position3,0x7808);
   offset.setDifference(current,precedingCar);
   float d=offset.squaredLength();
   if(d<=225.0f) detailLevel=2;
   else if(d<=625.0f) detailLevel=1;
  }
  char *car=func_0034C190(owner,carIndex);
  func_003D55C0(tiny,carIndex,F(self+carIndex*4,int,0x9684),car,detailLevel);
 }
 for(carIndex=0;carIndex<carCount;carIndex++) func_003CCC28(self,carIndex,owner,context);
 func_003CD920(self,owner);
 // Four animation channels store start, end, and inverse duration.
 for(channelIndex=0;channelIndex<4;channelIndex++) {
  float period=0x1.555554p-3f;
  AnimationQuery *o=&out; o->unknown0=0; o->unknown8=o->time=0; o->unknown12=0; o->unknown16=0; o->unknown20=0;
  if(func_00429F98(o,func_003CC808(self),F(self+channelIndex*0xAA4,int,0x38C4),D_006A3058)) {
   float a=out.time; a+=D_006A3060[channelIndex]; float b=a+period;
   setAnimationInterval((float *)(self+0x94C0)+channelIndex,a,b);
  } else {
   ((AnimationIntervals *)(self+0x94C0))->start[channelIndex]=-1.0f;
   ((AnimationIntervals *)(self+0x94C0))->end[channelIndex]=-1.0f;
   ((AnimationIntervals *)(self+0x94C0))->inverseDuration[channelIndex]=1.0f;
  }
  if(func_00429F98(&out,func_003CC808(self),F(self+channelIndex*0xAA4,int,0x38C4),D_006A3070)) {
   float a=out.time; a+=D_006A3078[channelIndex]; float b=a+period;
   setAnimationInterval((float *)(self+0x94F0)+channelIndex,a,b);
  } else {
   ((AnimationIntervals *)(self+0x94F0))->start[channelIndex]=-1.0f;
   ((AnimationIntervals *)(self+0x94F0))->end[channelIndex]=-1.0f;
   ((AnimationIntervals *)(self+0x94F0))->inverseDuration[channelIndex]=1.0f;
  }
 }
}
