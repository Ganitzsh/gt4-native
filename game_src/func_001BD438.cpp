/* compiler: ee-gcc2.96-no-strict-aliasing */
struct Rect { float x,y,w,h; Rect() {} Rect(float a,float b,float c,float d):x(a),y(b),w(c),h(d) {} };
struct Buf { int a[16]; };
struct Ventry { short delta; short pad; void (*fn)(); };
struct View { int a; Ventry *vt; };
struct Race { char pad[0x64]; Ventry *vt; };
struct Camera { Ventry *vt; char pad[0xe6c]; int a,b; float c; };
inline int project(View *o,Rect *r,Rect *in) { Ventry *e=o->vt+0x1f8/8; return ((int(*)(void *,Rect *,Rect *))e->fn)((char*)o+e->delta,r,in); }
inline Camera *camera(Race *o) { Ventry *e=o->vt+0x4b0/8; return ((Camera *(*)(void *))e->fn)((char*)o+e->delta); }
inline int active(Camera *o) { Ventry *e=o->vt+0x238/8; return ((int(*)(void *))e->fn)((char*)o+e->delta); }
extern char *D_00622F4C;
extern "C" {
void mWidget__virtual_70(void *,View *,int);
Race *func_001BD3C8(View *) throw();
float func_002313E0(View *); float func_00231400(View *); float func_00231420(View *); float func_00231440(View *);
void RaceEntryCar__update(int); void func_004A53F8(); void pglPushAttrib(int); void func_004A2980(int); void func_004A3078(int); void func_004A3088(float); void pglFrontFace(int);
void func_003C7DA8(Race *); int func_001052E0(void *,int,int,int); void func_00105338(void *,void *,int);
void func_0033E368(Race *, void *, void *, int, float); void func_003C7A50(Race *,void *); int func_003C7838(Race *);
float func_003784B8(Camera *,float,float,float,float);
void func_0033E6E0(Race *,void *,void *,void *,float,float,float);
void func_003C7BB0(Race *,void *); void func_001056A0(void *); void func_004AA1D8(float,float,float,float);
void func_004A1638(int); void func_004AB040(int); void func_004A2808(int,float); void func_003C73C0(Race *);
void func_004A5688(); void func_004A29A8(int); void func_004A4550(int,int); void func_00105728(void *);
void func_004A4810(int,int,int); void func_004A6668(float,float,float,float,float,float,float,float);
void func_004A5400(); void func_00105280(void *,int);
}
extern "C" void mPhotoViewFace__virtual_70(void *arg0, View *view, int enabled) {
 Rect input;
 mWidget__virtual_70(arg0,view,0);
 if(enabled) {
 Race *race=func_001BD3C8(view);
 if(race) {
 float x=func_002313E0(view);
 float y=func_00231400(view);
 float w=func_00231420(view);
 float h=func_00231440(view);
 input.x=x; input.y=y; input.w=w; input.h=h;
 Rect r(0.0f,0.0f,0.0f,0.0f);
 project(view,&r,&input);
 RaceEntryCar__update(1);func_004A53F8();RaceEntryCar__update(0);func_004A53F8();
 pglPushAttrib(0xc1);func_004A2980(-1);func_004A3078(0);func_004A3088(1.0f);pglFrontFace(0);
 Camera *cam=camera(race);
 int *params=&cam->a;
 int width=((int)r.w+2)&~3;
 int height=((int)r.h+2)&~3;
 func_003C7DA8(race);
 Buf b, c;
 func_001052E0(&b,0,width,height);func_00105338(&c,&b,0x31);
 func_0033E368(race, &b, &c, -1, 0.0f);func_003C7A50(race,&c);
 *(int*)((char*)race+0xe320)=0;
 float dist,fov;
 if(func_003C7838(race)) {
 float value=*(float*)(params+2);
 dist=(float)params[1]/100.0f;
 int flag=1;
 if(!(value<=0.0f)) flag=0;
 if(flag) fov=func_003784B8(cam,dist,35.0f,1.3333332538604736328125f,0x1.111110p-10f);
 else fov=value;
 } else {dist=200.0f;fov=10.0f;}
 func_0033E6E0(race,&b,&c,&b,35.0f,dist,fov);func_003C7BB0(race,&b);
 if(active(cam)) {
 func_001056A0(&b);func_004AA1D8(0.7999999523162841796875f,0.7999999523162841796875f,0.5f,1.0f);
 func_004A1638(6);func_004AB040(5);func_004A1638(9);func_004A2808(0x44,1.0f);func_003C73C0(race);func_004AB040(9);
 }
 func_004A5688();func_004AB040(6);func_004AB040(4);func_004AB040(5);func_004A29A8(0);
 func_004A4550(3,0);func_004A4550(2,0);func_00105728(&b);func_004AA1D8(1.0f,1.0f,1.0f,1.0f);
 float half=r.w*0.5f;
 float mid=r.x+half;
 float rad;
 if(*(int*)(D_00622F4C+0x38cc4)) rad=r.w*0.75f*0.5f; else rad=half;
 func_004A4810(0,0,b.a[4]+b.a[6]-1);func_004A4810(1,0,b.a[5]+b.a[7]-1);
 func_004A6668(mid-rad,r.y,mid+rad,r.y+r.h,0.0f,0.0f,(float)width,(float)(height-1));
 func_004A5400();RaceEntryCar__update(1);func_004A5400();RaceEntryCar__update(0);
 func_004AB040(15);func_004A1638(17);func_004A1638(6);func_004A2808(0x44,1.0f);
 func_00105280(&c,2);func_00105280(&b,2);
 }
 }
}
/* native correction of src/media/mPhotoViewFace/mPhotoViewFace__virtual_70.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_0033E368 */
