/* compiler: ee-gcc2.96-no-strict-aliasing */
#define I(p,o) (*(int *)((char *)(p)+(o)))
#define F(p,o) (*(float *)((char *)(p)+(o)))
#define P(p,o) (*(char **)((char *)(p)+(o)))
struct V { short d, i; void (*fn)(); };
#define slot(p,vo,off) ((V *)(P(p,vo)+(off)))
struct Rect { float x,y,w,h; int z; };
struct Color { float v[6]; };
class View;
class Race { char pad[0x64]; public:
virtual void m0();
virtual void m1();
virtual void m2();
virtual void m3();
virtual void m4();
virtual void m5();
virtual void m6();
virtual void m7();
virtual void m8();
virtual void m9();
virtual void m10();
virtual void m11();
virtual void m12();
virtual void m13();
virtual void m14();
virtual void m15();
virtual void m16();
virtual void m17();
virtual void m18();
virtual void m19();
virtual void m20();
virtual void m21();
virtual void m22();
virtual int check();
virtual void m24();
virtual View *view(int,float);
virtual void m26();
virtual void m27();
virtual void m28();
virtual void m29();
virtual void m30();
virtual void m31();
virtual void m32();
virtual void m33();
virtual void m34();
virtual void m35();
virtual void m36();
virtual void m37();
virtual void m38();
virtual void m39();
virtual void m40();
virtual void m41();
virtual void m42();
virtual void m43();
virtual void m44();
virtual void m45();
virtual void m46();
virtual void m47();
virtual void m48();
virtual void m49();
virtual void m50();
virtual void m51();
virtual void m52();
virtual void m53();
virtual void m54();
virtual void m55();
virtual void m56();
virtual void m57();
virtual void m58();
virtual void m59();
virtual void m60();
virtual void m61();
virtual void m62();
virtual void m63();
virtual void m64();
virtual void m65();
virtual void m66();
virtual void m67();
virtual void m68();
virtual void m69();
virtual void m70();
virtual void m71();
virtual void m72();
virtual void m73();
virtual void m74();
virtual void m75();
virtual void m76();
virtual void m77();
virtual void m78();
virtual void m79();
virtual void m80();
virtual void m81();
virtual void m82();
virtual void m83();
virtual void m84();
virtual void m85();
virtual void m86();
virtual void m87();
virtual void m88();
virtual void m89();
virtual void m90();
virtual void m91();
virtual void m92();
virtual void m93();
virtual void m94();
virtual void m95();
virtual void m96();
virtual void m97();
virtual void m98();
virtual void m99();
virtual void m100();
virtual void m101();
virtual void m102();
virtual void m103();
virtual void m104();
virtual void m105();
virtual void m106();
virtual void m107();
virtual void m108();
virtual void m109();
virtual void m110();
virtual void m111();
virtual void m112();
virtual void m113();
virtual void m114();
virtual void m115();
virtual void m116();
virtual void m117();
virtual void m118();
virtual void render(void *,int);
};
class View { public:
virtual void m0();
virtual void m1();
virtual void m2();
virtual void m3();
virtual void m4();
virtual void m5();
virtual void m6();
virtual void m7();
virtual void m8();
virtual void m9();
virtual void m10();
virtual void m11();
virtual void m12();
virtual void m13();
virtual void m14();
virtual void m15();
virtual void m16();
virtual void m17();
virtual void m18();
virtual void m19();
virtual void m20();
virtual void m21();
virtual void render(void *,void *);
virtual void m23();
virtual void m24();
virtual void m25();
virtual void m26();
virtual void m27();
virtual void m28();
virtual void m29();
virtual void m30();
virtual void m31();
virtual void m32();
virtual int flags();
virtual void m34();
virtual void m35();
virtual void m36();
virtual void m37();
virtual void m38();
virtual void m39();
virtual void m40();
virtual void m41();
virtual void m42();
virtual void m43();
virtual void m44();
virtual void m45();
virtual void m46();
virtual void m47();
virtual void m48();
virtual int hasColor();
virtual Color *getColor();
};
extern "C" {
void func_0033EA30(void *, void *) throw();
void func_00105280(void *, int);
int func_001052E0(void *, int, int, int);
void func_001054A0(void *, int);
void func_001056A0(void *);
void func_00105708(void *);
void func_00105728(void *);
void func_001057F8(void *);
void func_003B88C8(void *);
void func_003BBBC0(void *, unsigned, unsigned, float, float);
int Pitmen__isNotRenderOthersPeriod(void *, int);
void func_003E7CC8(void *, void *, int, int);
int func_003EFD08(void *);
int func_003EFEA8(void *);
int func_003EFEE8(void *);
void func_00496638(int,int);
void func_004A1638(int);
void func_004A1AB0(int);
void func_004A2808(int,float);
void func_004A4550(int,int);
void func_004A4810(int,int,int);
void func_004A53F8();
void func_004A5400();
void func_004A6668(float,float,float,float,float,float,float,float);
void func_004A74A0();
void func_004A7D90(float,float,float,float,float,float,float);
unsigned func_004AA0D0(float,float,float,float,float);
void func_004AA168(int);
void func_004AA1D8(float,float,float,float);
void func_004AB040(int);
}
static inline void color(Color *c,float a) { func_004A7D90(c->v[0],c->v[1],c->v[2],c->v[3],c->v[4],c->v[5],a); }
extern "C" void func_0033F2E8(char *self, int *rs, void *tex, int unused0, float unusedf0) {
    char *course=P(P(P(self,0x6c),0x80),4);
    char *data=P(course,0x80);
    char *view=(char *)((Race *)self)->view(0,unusedf0);
    func_0033EA30(self,rs);
    func_001056A0(rs);
    func_00105708(tex);
    if(I(self,0xe324)) {
        Rect r=*(Rect *)(self+0xe2e0);
        int state[16];
        F(self,0xe2e0)=0; F(self,0xe2e4)=0;
        func_001052E0(state,0,(int)F(self,0xe2e8),(int)F(self,0xe2ec));
        func_001054A0(state,9);
        ((Race *)self)->render(state,0);
        func_001056A0(rs); func_00105728(state);
        func_004AB040(5); func_004AB040(4); func_004AB040(3);
        func_004A1638(6); func_004AB040(8);
        func_004A2808(100,F(self,0xe328));
        float f=F(data,0x38); func_004AA1D8(f,f,f,1.0f);
        func_004A4550(4,2); func_004A4550(5,2);
        func_004A4810(0,0,(int)r.w-1); func_004A4810(1,0,(int)r.h-1);
        func_004A4550(2,1); func_004A4550(3,1);
        func_004A6668(r.x,r.y,r.x+r.w,r.y+r.h,0,0.5f,r.w,r.h+0.5f);
        func_00105280(state,2);
    }
    if(I(self,0xe344)) {
        func_004A53F8(); func_004A74A0();
        int mask=((View *)view)->flags() & 0x2000;
        if((I(data,0x10)&0x20) && (mask&0x2000)) {
            func_004A1AB0(2); func_00496638(I(self,0xd94),*rs); func_004A1AB0(0);
        }
        func_004A5400();
        if(Pitmen__isNotRenderOthersPeriod(self+0x3628,*(short *)(view+I(view,0x9bc)*0x19c+0x1c)))
            func_003E7CC8(self+0x31d0,tex,0,*(short *)(view+I(view,0x9bc)*0x19c+0x1c));
        else func_003E7CC8(self+0x31d0,tex,0,-1);
        func_003B88C8(self+0xda0);
    }
    if(I(self,0xe33c)) {
        ((View *)view)->render(P(self,0x6c),rs);
    }
    func_004AB040(5); func_001056A0(rs); func_00105728(rs); func_004AA168(0x808080);
    if(I(self,0xe320)) color((Color *)(self+0xe308),255.0f);
    else {
        if(((View *)view)->hasColor()) {
            color(((View *)view)->getColor(),220.0f);
        } else if(I(data,0x10)&0x80) func_004A7D90(F(data,0x980),F(data,0x984),F(data,0x988),F(data,0x98c),F(data,0x990),F(data,0x994),235.0f);
        else {
            Color c;
            if(!I(self,0xcc8)) func_003EFEA8(&c); else func_003EFEE8(&c);
            if(!func_003EFD08(&c)) color(&c,255.0f);
        }
    }
    if(I(self,0xcc8) && (I(data,0x10)&0x40)) {
        if(!((Race *)self)->check()) {
            float a=F(data,0x950), b=F(data,0x954);
            unsigned c=func_004AA0D0(F(data,0x958),F(data,0x95c),F(data,0x960),F(data,0x964),255);
            unsigned d=func_004AA0D0(F(data,0x968),F(data,0x96c),F(data,0x970),F(data,0x974),255);
            func_003BBBC0(rs, c, d, a, b);
        }
    }
    func_004AB040(15); func_004AB040(4); func_004AB040(3); func_004AB040(6);
    func_001057F8(rs);
}
/* native correction of src/race/RaceBGMPS2_RacePS2Base/func_0033F2E8.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_003BBBC0 */
