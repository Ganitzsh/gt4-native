/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native correction of the provisional draft drafts/00217058/00217058_05fa655b822e57a9.cpp (argument widths, argcheck.py width/retwidth): func_00216E78 takes `int n`; the draft declared it long (8 bytes natively), which shifted flag, e and mode one slot. */
struct Vec { float x,y,z,w; Vec() {} Vec(float a,float b,float c,float d) {x=a;y=b;z=c;w=d;} };
struct View { int a,b,width,c,x,y,w,h; char pad[0x1c]; };
struct Pair { View v[2]; int cur; View *current(){return v+cur;} };
struct Global { Pair p; char pad[0x7c]; char buf[0x40]; };
struct Buf { int a,b,tex; char pad[0x34]; };
struct Entry { short delta,index; void (*fn)(void *); };
struct Entry3 { short delta,index; int (*fn)(void *,Vec *,Vec *); };
struct Entry1 { short delta,index; void (*fn)(void *,int); };
struct Obj { int a; char *vt; char pad[0x40]; char *vt48; char pad4c[0x1d54]; int state; };
struct Camera { float a,b,n,f; };
struct Tex { char pad[0x18]; int tex; };
extern "C" {
void func_00105280(void *,int); void *func_00105378(void *,void *); void func_001054A0(void *,int); int func_00105528(void *);
void func_001055C0(void *);void func_00105670(void *);void func_001056A0(void *);void func_00105728(void *);int func_00105818(void *);
void func_00105708(void *); void func_00107328(void *);
void func_00107AE8(void *); void func_00107B00(void *,int);
void func_00107BE8(void *,void *); void func_001FE5B0(void *,void *);
float func_00201FA0(void *); Vec *func_0021A760(void *);
Camera *func_0021A798(void *); Obj *func_0021A7C8(void *);
float func_0021A7E8(void *);int func_0021A7F8(void *);int func_0021A808(void *);float func_002317C8(void *);
void func_00216E78(Vec *,Obj *,View *,View *,View *,int,int,Obj *,int) throw();
void func_0049CE68(int); void func_004A1638(int); void func_004A26F0(int);
void func_004A2808(int,float); void func_004A2930(int,int,int,int);
void func_004A2980(int);void func_004A3078(int);void func_004A29A8(int); void func_004A3230(int); void func_004A4550(int,int);
void func_004A5348(int); void func_004A53F8(); void func_004A5400();
void func_004A5418(int); void func_004A5688();
void func_004A59B8(float,float,float,float,float,float,float);
void func_004A60B0(float,float,float,float);void func_0049F7D8(float,float,float,float,float,float);
void func_004A6788(float,float,float,float,float,float,float,float);
void func_004A7454(); void func_004AA1D8(float,float,float,float);void func_004AA4A0(); void func_004AB040(int);
float func_0057D380(float);
extern char D_00618E40[],D_006184D0[];
extern Global D_0061850C;
}
static inline Tex *texture(Obj *r) { Entry *t=(Entry *)(r->vt+0x80);return ((Tex *(*)(void *))t->fn)((char *)r+t->delta); }
static inline View *choose(const int &k,View *a,View *b){return k?a:b;}
static inline void render(Vec *v,Obj *o,View *a,View *b,View *c,int n,int first,Obj *extra,const int &flag){func_00216E78(v,o,a,b,c,n,first,extra,flag);}
static inline void draw(const float &x,const float &y,float w,float h,View *out) {float ox=out->x,ex=out->x+out->w,oy=out->y,ey=out->y+out->h;func_004A6788(x,y,w,h,ox,oy,ex,ey);}
extern "C" void func_00217058(Obj *self,Obj *o,Obj *extra) {
 Vec v;Vec *p=&v;Vec *q=func_0021A760(o);
 p->x=q->x;p->y=q->y;p->z=q->z;p->w=q->w;
 Vec zero(0,0,0,0);
 Entry3 *e3=(Entry3 *)(self->vt+0x1f8);e3->fn((char *)self+e3->delta,&zero,p);
 int count=func_0021A808(o); if(count==0 || count==2) count=2;else count=4;
 func_004AA4A0();float scale=0.3f;
 if(self->state==0) {
 func_004A5418(0x887);func_004AB040(8);func_004A1638(5);func_004A26F0(3);func_004A1638(100);func_004AB040(8);func_004A4550(1,0);func_004A2930(1,1,1,1);
 Global *g=&D_0061850C;View *view=g->p.current();
 View a,b;func_00105378(&a,view);func_00105378(&b,view);
 a.b=0;func_00105528(&a);func_00105670(&a);b.b=0;func_00105528(&b);func_001056A0(&b);func_00105708(g->buf);
 Buf buf;func_00107AE8(&buf);Obj *r=func_0021A7C8(o);
 if(r){Tex *tex=texture(r);func_00107BE8(&buf,tex);func_0049CE68(tex->tex);}else{func_001FE5B0(D_00618E40,&buf);func_0049CE68(buf.tex);}
 int separate=0;func_004A60B0(0,0,0,0);func_0049F7D8(0,1,0,1,0,0);
 Camera *c=func_0021A798(o);func_004A5348(1);func_004A53F8();func_004A7454();
 float height,x,y,h,width,n,f; width=view->width;h=zero.w*0.5f;x=zero.x+zero.z*0.5f;y=zero.y;height=func_00105818(view);n=c->n;y+=h;f=c->f;width-=x;height=y-height;
 func_004A59B8(-x,width,height,y,n,f,h/func_0057D380(func_00201FA0(c)*0.5f*3.14159265f/180.0f));
 func_00107328(D_006184D0);func_004A5348(0);func_004A53F8();func_004A2980(0xffffff);func_004A3078(0x80000000);func_004A3230(1);func_004A2980(-1);func_004A7454();
 Entry *e=(Entry *)(o->vt48+0x18);e->fn((char *)o+e->delta);
 int dual=func_0021A7F8(o);
 if(extra && (dual || func_0021A7E8(extra)<0.99f))separate=1;
 float alpha=func_0021A7E8(o);
 for(int i=0;dual?i<2:i<1;i++) {
 int first=dual?(i==0):0;
 func_004A53F8();Entry1 *e1=(Entry1 *)(o->vt48+0x10);e1->fn((char *)o+e1->delta,first);
 Entry *e2=(Entry *)(o->vt48+0x20);e2->fn((char *)o+e2->delta);
 func_00216E78(&zero,o,&b,view,&a,first?count/2:count,first,separate?0:extra,0);
 if(separate)render(&zero,extra,&b,&a,&a,1,first,0,1);
 func_004A5400();View *out=choose(1,&a,&b);func_00105728(out);
 View tmp;func_00105378(&tmp,view);func_001054A0(&tmp,0);func_001056A0(&tmp);
 func_004A4550(3,0);func_004A4550(2,0);func_004A29A8(0);func_004AB040(5);func_004AB040(4);func_004A1638(6);
 func_004AA1D8(1,1,1,func_002317C8(self));
 if(first)func_004A2808(104,alpha*scale);else func_004A2808(100,alpha);
 float w=tmp.width;float ht=func_00105818(&tmp);
 draw(0,0,w,ht,out);
 func_00105280(&tmp,2);
 }
 func_0049CE68(0);func_004A5348(0);func_004A5400();func_004A5348(1);func_004A5400();func_001055C0(&a);func_001055C0(&b);func_001056A0(view);
 if(self->state==0)func_00105708(D_0061850C.buf);
 func_004A1638(6);func_004A2808(68,1);func_004AB040(5);func_004AB040(4);func_004AB040(100);func_004A29A8(0);func_004A5688();func_004A2930(1,1,1,0);
 func_00107B00(&buf,2);func_00105280(&b,2);func_00105280(&a,2);
 }
}
