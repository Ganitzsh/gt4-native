/* compiler: ee-gcc2.96-no-strict-aliasing */
struct Obj {
 union { unsigned int word; struct { unsigned char kind,xflag,yflag,pad3; } b; } u;
 short x,y,w,h;
 float opacity;
 int m10;
 void *vptr;
};
extern "C" char RaceDisplayObjectBase__vtable[];
extern "C" int RaceDisplayObjectBase__structor_0(Obj *p) {
 p->vptr = RaceDisplayObjectBase__vtable;
 p->u.word &= ~0xffu;
 int r = p->u.word;
 p->u.b.xflag = 2;
 p->u.b.yflag = 2;
 p->x = 0;
 p->y = 0;
 p->w = 0;
 p->h = 0;
 p->opacity = 1.0f;
 p->m10 = 0;
 return r;
}
/* native correction of src/race/RaceDisplayObjectBase/RaceDisplayObjectBase__structor_0.cpp: returns the masked first word (word & ~0xFF), the value $v0 holds at its return on the PS2, which RaceRefuelMeter__structor_1 returns. */
