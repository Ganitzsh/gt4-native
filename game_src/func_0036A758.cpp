/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native correction of the provisional draft build/swarm/x3/0036a758_best.cpp (argument order, argcheck.py orders): func_00369DE8 is (five ints, two floats) as defined; the draft's order is the same EE call but other stack slots natively. */
typedef unsigned char u8;
#define F(p,o) (*(float *)((u8 *)(p)+(o)))
struct Map { u8 kind,n,m,pad; u8 data[0x48]; };
struct Wheel {float f[0xEC/4];};
struct Self {u8 pad[0x164]; Wheel wheels[4];};
struct Params { u8 pad[8]; float f[0x500]; };
extern "C" Map *func_00359510(Params *);
extern "C" float func_0035D248(u8 *, int);
extern "C" void func_00369DE8(u8 *,u8 *,u8 *,int,int,float,float) throw();
extern "C" float func_0036A690(Params *,int,float,float) throw();
extern "C" float func_0036A578(Params *,int,float,float) throw();
extern "C" float func_0036A048(u8 *,u8 *,u8 *) throw();
extern "C" float func_0057D1F0(float);
extern u8 D_00620CD8[];
extern u8 D_00620E11;
static inline int positive(Map *m,int i) { if(i>=m->n) return 0; return (signed char)m->data[0x38+i]>0; }
static inline int negative(Map *m,int i) { if(i>=m->n) return 0; return (signed char)m->data[0x38+i]<0; }
static inline int param(Map *m,int i) { if(i>=m->n) return 0; return m->data[0x10+i]; }
static inline int param2(Map *m,int i) { if(i>=m->m) return 0; return m->data[0xC+i]; }
extern "C" void func_0036A758(u8 *self) {
    float forces[4];
    u8 *s=self+0x104;
    Params *p=*(Params **)(self+0x10);
    Map *m=func_00359510(p);
    int i;
    i=0;
    if(i<m->n) {
    u8 *r=self+0x7B8;
    Wheel *cursor=&((Self *)self)->wheels[0];
    for(;i<m->n;i++,r+=0x34,cursor++) {
        int a=m->data[0x28+i];
        float x=p->f[8+a]*F(s,0x584);
        float y=F((u8*)p+4*a,0x10)*F(s,0x580);
        float v=0.0f;
        if(m->data[0x18+i]==0) v+=x; else if((m->data[0x18+i]^1)==0) v-=x;
        if(positive(m,i)) v+=y; else if(negative(m,i)) v-=y;
        Wheel *w=cursor;
        w->f[0xD8/4]=(F(s,0x544)+F(s,0x24)*v)*F(r,0x1C)+(F(s,0x548)+F(s,0x28)*v)*-F(r,0x24)+(F(s,0x54C)+F(s,0x2C)*v)*F(r,0x20);
    }
    }
    i=0;
    if(i<m->n) {
        float zero=0.0f;
        float *height=(float *)(s+0x410);
        u8 *cursor=self+0x164;
        u8 *indices=m->data+0x10;
        for(;i<m->n;i++,cursor+=0xEC,height++,indices++) {
            int a=0;
            if(i<m->n) a=*indices;
            float v=*height;
            signed char state=1;
            u8 *q=(u8*)p+a*0x58+0x308;
            u8 *w=cursor;
            if(!(F(w,0x48)>zero)) state=0;
            func_00369DE8(self,w,q,i,a,v,func_0035D248(self,i));
            if(state==0 && F(w,0x48)>zero) state=2;
            else if(state==1 && F(w,0x48)==zero) state=3;
            w[0xDC]=state;
            v=F(w,0x94)-F(q,0x10);
            F(w,8)=func_0036A690(p,i,F(s,0x468),v);
            if(m->kind!=1) F(w,4)=func_0036A578(p,i,F(s,0x468),v);
            else {v=F(p,0x1004); v*=func_0057D1F0(F(s,0x56C)-F(s,0x578)); F(w,4)=F(s,0x468)*v;}
        }
    }
    for(i=0;i<m->m;i++) {
        forces[i]=0.0f;
        int a=m->data[8+i];
        int b=m->data[0x20+a];
        u8 *w=self+0xEC*a;
        u8 *w2=self+0xEC*b;
        if(b!=0x7F) forces[i]=func_0036A048(w+0x164,w2+0x164,(u8*)p+param2(m,i)*0x58+0x308);
    }
    float v7=0.0f;
    if(!D_00620E11) v7=F(p,0x34)*-F(s,0x554);
    F(s,0x5B8)=0.0f;
    if(m->kind==1) F(s,0x5B8)=F(s,0x554)*F(s,0x4D4)*F(s,0x2C);
    u8 *p14=(u8*)p+0x14;
    float v6=F(s,0x564);
    v6=F(p,0x38)*v6;
    for(i=0;i<m->n;i++) {
        Wheel *w=&((Self *)self)->wheels[i];
        float v=w->f[0x9C/4];
        if(w->f[0x48/4]==0.0f) v=-F(p14,0x330+param(m,i)*0x58);
        if(m->data[0x20+i]!=0x7F) {
            float f=forces[m->data[0x28+i]];
            if(m->data[0x18+i]==0) v-=f; else if((m->data[0x18+i]^1)==0) v+=f;
        }
        float f=v6;
        if(positive(m,i)) {
            int n=m->data[0];
            v-=F(s,0x5A8)/(float)n;
            if(n!=2) f*=2.0f/(float)n;
            v+=f;
        } else if(negative(m,i)) {
            int n=m->data[1];
            v-=F(s,0x5AC)/(float)n;
            if(n!=2) f*=2.0f/(float)n;
            v-=f;
        }
        if(m->data[0x20+i]!=0x7F) {
            if(D_00620CD8[0x139]) {
                int a=param(m,i);
                float x=F(s,0x410+i*4)-F(p14,0x300+0x58*a)+F(p,0x2C4+4*a);
                x=(F(p,0x1060)+x)/F(p,0x28+4*a);
                x*=w->f[0xD4/4];
                if(m->data[0x18+i]==0) v-=x; else if((m->data[0x18+i]^1)==0) v+=x;
            } else {
                if(m->data[0x18+i]==0) v+=v7; else if((m->data[0x18+i]^1)==0) v-=v7;
            }
        }
        F(s,0x420+i*4)=v;
    }
}
