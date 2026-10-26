/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native correction of the provisional draft build/swarm/a12/0040fc78_best.cpp (argument order, argcheck.py orders): its matched callers func_00410630 and func_00410E60 pass the four floats second, before active; the draft's order is the same EE function but natively read the floats from other stack slots. */
extern "C" float func_0040F5C0(int,float) throw() __attribute__((const));
extern "C" float func_0040F7C8(float) throw() __attribute__((const));
extern "C" float func_0040F718(float,float) throw();
static inline float minf(const float &a,const float &b) {float r;__asm__("min.s %0, %1, %2":"=f"(r):"f"(a),"f"(b));return r;}
static inline float maxf(const float &a,const float &b) {float r;__asm__("max.s %0, %1, %2":"=f"(r):"f"(a),"f"(b));return r;}
static inline float clamp(const float &x,const float &lo,const float &hi) {return minf(maxf(x,lo),hi);}
struct Line {float coeff[2];};
static inline void init(Line *p,float a,float b) {p->coeff[0]=a;p->coeff[1]=b;}
static inline void init_limits(float *lo,float *hi,float a,float b) {*lo=a;*hi=b;}
static inline float linear(Line *p,float x) {return p->coeff[0]*x+p->coeff[1];}
extern "C" void func_0040FC78(int side,float angle,float delta,float speed,float amount,int active,float *out,float *target,float *flag,float *current,int unused6,float *other,float *distance,float *reference,float *blendA,float *blendFactor,int unused12,int unused13,int unused14,int unused15,int *state,int *mode) {
 float mag=__builtin_fabsf(amount);
 float bounds[2];
 if(side==0) bounds[0]=-0x1.7e0484p+1f; else bounds[0]=-0x1.69e954p+1f;
 if(side==0) bounds[1]=0x1.69e954p+1f; else bounds[1]=0x1.7e0484p+1f;
 float l=bounds[0]+mag*0x1.999998p-4f;
 float h=bounds[1]-mag*0x1.999998p-4f;
 if(*mode!=2) {bounds[0]=l;bounds[1]=h;}
 bounds[0]=minf(bounds[0],-0x1.2d97c6p+1f);
 bounds[1]=maxf(bounds[1],0x1.2d97c6p+1f);
 int again;
 do {
  again=0;
  switch(*state) {
  case 0:
   *current+=delta;
   if(active) {
    if(*current<bounds[0]) {*current=bounds[0];*state=1;}
    else if(bounds[1]<*current) {*current=bounds[1];*state=1;}
   }
   {float v=*current;*out=v;*target=v;} *flag=0;
   break;
  case 1:
   if((unsigned)(*mode-2)<2 || *mode==5 || *mode==6) {*state=0;again=1;break;}
   if(mag<0x1.921fb4p+1f) {
    Line line;init(&line,-0x1.c71c7ep-4f,0x1.9b0f52p+0f);
    float lo,hi;init_limits(&lo,&hi,0x1.41b2f4p-2f,0x1.921fb4p+0f);
    float a=clamp(linear(&line,mag),lo,hi);
    if(*out<0) a=-a;
    *target=a;
   } else {
    Line line;init(&line,0x1.11110ep-4f,-0x1.acee9ap-3f);
    float lo,hi;init_limits(&lo,&hi,0x1.41b2f4p-2f,0x1.197c96p+0f);
    float a=clamp(linear(&line,mag),lo,hi);
    if(!(*out<0)) a=-a;
    *target=a;
   }
   *flag=0;
   if((side==0 && *reference<*target) || (side==1 && *target<*reference)) *state=2;
   else *state=3;
   *distance=__builtin_fabsf(*target-*out);
   if(*distance<0x1.41b2f4p-2f) *state=0;
   *current=*out;
   again=1;
   break;
  case 2:case 3: {
   float t=clamp(__builtin_fabsf(*target-*current)/ *distance,0.0f,1.0f);
   if(*state==3) {*flag=1.0f-t;*other=func_0040F7C8(t)*0x1.999998p-4f;}
   else {*flag=0;*other=func_0040F7C8(t);}
   int passed=0;
   if(*current<*target) {
    *current+=func_0040F718(t,amount)*speed;
    if(*target<*current)passed=1;
   } else {
    *current-=func_0040F718(t,amount)*speed;
    if(*current<*target)passed=1;
   }
   if(*state==2) *out=*current;
   if(passed) {
    float v=*target;*out=v;*current=v;*flag=0;*other=0;*state=0;*mode=4;
   }
   break;
  }
  case 4: {
   if((unsigned)(*mode-2)<2 || *mode==5 || *mode==6) {*state=0;again=1;break;}
   float blended=*reference*(1.0f-*blendFactor)+*blendA* *blendFactor;
   *flag=0;
   *target=func_0040F5C0(side,blended);
   if((side==0 && *reference<*target) || (side==1 && *target<*reference)) *state=5;
   else *state=6;
   *distance=__builtin_fabsf(*target-*out);
   if(*distance<0x1.41b2f4p-2f) *state=0;
   *current=*out;
   again=1;
   break;
  }
  case 5:case 6: {
   float t=clamp(__builtin_fabsf(*target-*current)/ *distance,0.0f,1.0f);
   if(*state==6) {*flag=1.0f-t;*other=0;}
   else {*flag=0;*other=func_0040F7C8(t);}
   int passed=0;
   if(*current<*target) {
    *current+=func_0040F718(t,amount)*speed;
    if(*target<*current)passed=1;
   } else {
    *current-=func_0040F718(t,amount)*speed;
    if(*current<*target)passed=1;
   }
   if(*state==5) *out=*current;
   if(passed) {float v=*target;*out=v;*current=v;*flag=0;*other=0;*state=0;}
   break;
  }
  case 7:
   *state=8;
   if(side==0) *target=-0x1.197c96p+0f;else *target=0x1.197c96p+0f;
   *distance=__builtin_fabsf(*target-*out);
   if(*distance<0x1.41b2f4p-3f) *state=0;
   *current=*out;
   again=1;
   break;
  case 8: {
   if(*mode==2) {*state=0;*mode=4;break;}
   int passed=0;
   float t=clamp(__builtin_fabsf(*target-*current)/ *distance,0.0f,1.0f);
   *other=func_0040F7C8(t);
   if(*current<*target) {
    *current+=func_0040F718(t,amount)*speed;
    if(*target<*current)passed=1;
   } else {
    *current-=func_0040F718(t,amount)*speed;
    if(*current<*target)passed=1;
   }
   *out=*current;
   if(passed) {float v=*target;*out=v;*current=v;*flag=0;*other=0;*current=*target;*state=0;}
   break;
  }
  default:break;
  }
 }while(again);
}
