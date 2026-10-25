/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Course-position bookkeeping for one car: given the car's new distance along the course (pos), compares it with the
 * stored one (old), handles wrap-around at the start/finish line (when the jump is more than half the course length the
 * lap counter moves and the position is shifted by a whole course length) and reports the times at which checkpoint
 * gates, data-logger gates, the start line and the finish line were crossed. Crossing times are interpolated inside the
 * frame (ratio * 50 sub-steps) added to the owner's tick, then divided by 3 (inferred from the constants; the unit of
 * tick is purpose unknown). Gates are crossed while pos decreases toward them (pos < gate <= old), so the course
 * coordinate runs downward along the direction of travel (inferred from those comparisons). Results go out through the
 * owner's virtual methods v5/v8/v15/v17-v22/v37/v38/v42, whose real names are not known here. The return value is 1 when
 * the position jumped by more than half a course length while decreasing, or increased without wrapping; the game
 * meaning of that flag is purpose unknown.
 * The 42-slot virtual list is declared in full only because the call slots must land on the right vtable offsets. */
struct Drive { char p[0x4a4]; float position; char q[4]; short lap; unsigned char gate,pad4af,back,pad4b1,blocked,pad4b3,logger; char r[0x5c1-0x4b5]; unsigned char flag; char t[0x614-0x5c2]; int time; char u[0x650-0x618]; float delta; char v[7]; unsigned char start; };
struct Position {char p[0x114]; float value; void adjust(float v) {value+=v;} void subtract(float v) {value-=v;} };
struct Car { char p[0x104]; Drive d; char q[0x888-0x104-sizeof(Drive)]; Position pos; };
struct Config { char p[0xe0]; int log0,log1; };
struct Owner { Config* config; char p[0xf898-4]; unsigned tick; char q[0x10140-0xf89c];
virtual int v0();
virtual int v1();
virtual int v2();
virtual int v3();
virtual int v4();
virtual int v5();
virtual int v6();
virtual int v7();
virtual int v8(Car*,int);
virtual int v9();
virtual int v10();
virtual int v11();
virtual int v12();
virtual int v13();
virtual int v14();
virtual unsigned v15(int);
virtual int v16();
virtual void v17(int,int,int,unsigned,int);
virtual void v18(int,int,int,unsigned,unsigned,int);
virtual void v19(int,unsigned,int,float);
virtual void v20(int,int,unsigned,unsigned,int,int,float);
virtual void v21(int,unsigned,float,int);
virtual int v22(int,int,unsigned,unsigned,int);
virtual int v23();
virtual int v24();
virtual int v25();
virtual int v26();
virtual int v27();
virtual int v28();
virtual int v29();
virtual int v30();
virtual int v31();
virtual int v32();
virtual int v33();
virtual int v34();
virtual int v35();
virtual int v36();
virtual float v37();
virtual float v38();
virtual int v39();
virtual int v40();
virtual int v41();
virtual float v42(int);
};
extern "C" Car* func_0034C190(Owner*,int);
extern "C" float DynamicsConductor__GetCourseLength(Owner*);
extern "C" int DynamicsConductor__IsExistStartV(Owner*);
extern "C" int func_0034C470(Owner*);
extern "C" float func_00359AD8(Owner*,int);
extern "C" int func_00359AF0(Car*);
extern "C" void Automobile__isDataLoggerActive(Car *, int, int, float, float, float);
extern "C" void func_00359C10(Car*,int,int,int);
extern "C" int func_00358608(Owner* self,int index,float pos) {
 Car*car=func_0034C190(self,index);
 Drive*d=&func_0034C190(self,index)->d;
 float length=DynamicsConductor__GetCourseLength(self);
 float old=d->position;
 int result=0;
 if(pos<=old) {
  if(pos+length*0.5f<old) {
   result=1;
   d->delta=(old-(pos+length))*60.0f;
   car->pos.adjust(length);
   if(++d->back>250)d->back=250;
  } else {
   float distance=old-pos;
   d->delta=distance*60.0f;
   if(!d->back && d->lap && !d->blocked) {
    float gate=self->v42(d->gate);
    if(pos<gate && gate<=old) {
     unsigned rawTicks=self->tick+(int)(((gate-pos)/distance)*50.0f);
     rawTicks+=self->v15(index);
     unsigned time=rawTicks/3U;
     int splitTime=time-d->time;
     if(!self->v5())self->v17(index,d->lap-1,d->gate,time,splitTime);
     self->v18(index,d->lap,d->gate,rawTicks,time,splitTime);
     d->gate++;
    }
    if(func_00359AF0(car) && self->config->log0) {
     float gate=func_00359AD8(self,d->logger);
     if(pos<gate && gate<=old) {
      float ratio=(gate-pos)/distance;
      int subStep=(int)(ratio*50.0f);
      unsigned rawTicks=self->tick+subStep;
      rawTicks+=self->v15(index);
      unsigned time=rawTicks/3U;
      int splitTime=time-d->time;
      func_00359C10(car,self->v8(car,1),splitTime,self->config->log1);
      d->logger++;
      int subStepsLeft=50-subStep;
      int checkpointNo=self->v8(car,0);
      if(self->config->log1)Automobile__isDataLoggerActive(car, checkpointNo, subStepsLeft, ratio, gate, old);
     }
    }
    if(d->start==1 && DynamicsConductor__IsExistStartV(self)) {
     float gate=self->v37();
     if(pos<gate && gate<=old) {
      float ratio=(gate-pos)/distance;
      int subStep=(int)(ratio*50.0f);
      unsigned time=self->tick+subStep;
      int subStepsLeft=50-subStep;
      time+=self->v15(index);
      time/=3U;
      self->v21(index,time,ratio,subStepsLeft);
      d->start=2;
     }
    }
    if(func_0034C470(self)) {
     float gate=self->v38();
     if(pos<gate && gate<=old) {
      unsigned rawTicks=self->tick+(int)(((gate-pos)/distance)*50.0f);
     rawTicks+=self->v15(index);
      unsigned time=rawTicks/3U;
      int splitTime=time-d->time;
      int loggerActive=func_00359AF0(car);
      int lineCrossed=self->v22(index,d->lap,rawTicks,time,splitTime);
      if(lineCrossed && !self->v5())self->v17(index,d->lap-1,d->gate,time,splitTime);
      if(loggerActive && lineCrossed && self->config->log0)func_00359C10(car,self->v8(car,1),splitTime,self->config->log1);
     }
    }
   }
  }
 } else {
  if(old+length*0.5f<pos) {
   d->delta=((old+length)-pos)*60.0f;
   car->pos.subtract(length);
   if(d->back) {d->back--;}
   else if(!d->blocked) {
    float ratio=(length-pos)/((length-pos)+old);
    int gate=d->gate;
    int lap=d->lap;
    d->lap++;
    d->gate=0;
    int subStep=(int)(ratio*50.0f);
    unsigned rawTicks=self->tick+subStep;
    rawTicks+=self->v15(index);
    unsigned time=rawTicks/3U;
    int splitTime=time-d->time;
    int subStepsLeft=50-subStep;
    d->flag=0;
    if(lap==0) {
     self->v19(index,time,subStepsLeft,ratio);
     if(func_00359AF0(car) && !self->v6() && self->config->log1)Automobile__isDataLoggerActive(car, 0, subStepsLeft, ratio, 0.0f, old);
    } else {
     if(!self->v5())self->v17(index,lap-1,gate,time,splitTime);
     int loggerActive=func_00359AF0(car);
     self->v20(index,lap,rawTicks,time,splitTime,subStepsLeft,ratio);
     if(loggerActive && self->config->log0) {
      int checkpointNo=self->v8(car,1);
      func_00359C10(car,checkpointNo,splitTime,self->config->log1);
      if(self->v7() && !d->blocked)Automobile__isDataLoggerActive(car, checkpointNo+1, subStepsLeft, ratio, 0.0f, old);
     }
    }
    d->logger=0;
   }
  } else {d->delta=(old-pos)*60.0f;result=1;}
 }
 return result;
}
/* native correction of src/race/unit_0034E9A0/func_00358608.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes Automobile__isDataLoggerActive */
