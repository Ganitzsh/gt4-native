/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native copy of the provisional draft for 0021F6C8: the movie's slot-0x48 virtual gets enabled, which the EE forwards to it in $a2 ($a1 then holds the movie pointer) (passthrough_lint) */
struct Vec4;
extern "C" void func_002030A0(Vec4 *);
extern "C" Vec4 *func_002030E8(Vec4 *, Vec4 *);
extern "C" void func_00203118(Vec4 *, int);
struct Vec4 { float x,y,z,w; Vec4(){func_002030A0(this);} ~Vec4(){func_00203118(this,2);} };
struct Quad { int x0,y0; float x1; int y1,x2; float y2,x3,y3; Quad(float *w,float *h){x0=0;y0=0;x1=*w;y2=*h;x3=x1;y3=y2;y1=0;x2=0;} Quad(int a,int b,float c,int d,int e,float f,float g,float h){x0=a;y0=b;x1=c;y1=d;x2=e;y2=f;x3=g;y3=h;} };
struct Obj { int word; char *vt; };
struct Movie { char pad[0xB4]; int color; char padb[0x5C]; Obj *movie; };
struct EV { short d,i; void (*fn)(void *); };
struct EI { short d,i; int (*fn)(void *); };
struct EF { short d,i; float (*fn)(void *); };
static inline void vc(Obj *p,int off){EV *e=(EV *)(p->vt+off);e->fn((char *)p+e->d);}
static inline int vi(Obj *p,int off){EI *e=(EI *)(p->vt+off);return e->fn((char *)p+e->d);}
static inline int vi_fwd(Obj *p,int off,int a2){EI *e=(EI *)(p->vt+off);return ((int (*)(void *,Obj *,int))e->fn)((char *)p+e->d,p,a2);}
static inline float vf(Obj *p,int off){EF *e=(EF *)(p->vt+off);return e->fn((char *)p+e->d);}
static inline void color(Obj *p,int c){struct E {short d,i; void (*fn)(void *,int);};E *e=(E *)(p->vt+0x1c0);e->fn((char *)p+e->d,c);}
static inline void setvec(Obj *p,Vec4 *v){struct E {short d,i;void (*fn)(void *,Vec4 *);};E *e=(E *)(p->vt+0x198);e->fn((char *)p+e->d,v);}
static inline void draw(Obj *p,Quad *a,Quad *b,Vec4 *c){struct E {short d,i;void (*fn)(void *,int,Quad *,Quad *,Vec4 *);};E *e=(E *)(p->vt+0x1b0);e->fn((char *)p+e->d,4,a,b,c);}
extern "C" void func_002A2608(Movie *,Obj *,int);
extern "C" void func_0025B498(Movie *,float *,float *);
extern "C" int func_00286060(Movie *);
extern "C" Vec4 *func_002861D8(Movie *,int);
extern "C" void func_0021F6C8(Movie *self,Obj *ctx,int enabled){
 int ready=0; Obj **pm=&self->movie;
 if(*pm) ready=vi_fwd(*pm,0x48,enabled)!=0;
 func_002A2608(self,ctx,ready^1);
 if(enabled && ready){
 vc(*pm,0x68);vc(ctx,0x1d0);color(ctx,self->color);vc(*pm,0x78);
 float w,h;func_0025B498(self,&w,&h);
 Quad dest(&w,&h);
 float u=vf(*pm,0x80);float v=vf(*pm,0x88);
 Quad tex(0,0,u,0,0,v,u,v);
 Vec4 colors[4]; Vec4 *cp;
 if(func_00286060(self)==1){setvec(ctx,func_002861D8(self,0));cp=0;}
 else if(func_00286060(self)==4){func_002030E8(colors,func_002861D8(self,0));func_002030E8(colors+1,func_002861D8(self,1));func_002030E8(colors+2,func_002861D8(self,3));func_002030E8(colors+3,func_002861D8(self,2));cp=colors;}
 else cp=0;
 draw(ctx,&dest,&tex,cp);vc(ctx,0x1c8);vc(ctx,0x1d8);vc(*pm,0x70);
 }
}
