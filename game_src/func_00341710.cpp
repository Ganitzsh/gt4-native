/* compiler: ee-gcc2.96-nogcse */
/* Native copy of RacePS2Base::render_reflection_cars (00341710, matched): the cars mirrored in a wet road. The car
 * model's virtuals at +0xB8 (003907D0: lod, rate) and +0xF0/+0xF8 (00390F70: p, q, rate) take their ints before the
 * float; the matched source passes the float first, the same call on the EE (floats in their own registers) but
 * not natively: the reflection's rate landed in the level-of-detail slot and the float got the 1's bits. */
typedef int s32; typedef short s16; typedef float f32;
#define F(p,t,o) (*(t *)((char *)(p)+(o)))
struct E { s16 delta,index; void *fn; };
struct EF { s16 delta,index; f32 (*fn)(char *,f32); };
struct EVF { s16 delta,index; void (*fn)(char *,f32); };
struct EVP { s16 delta,index; void (*fn)(char *,char *); };
struct EVI { s16 delta,index; void (*fn)(char *,s32); };
struct EVIF { s16 delta,index; void (*fn)(char *,s32,f32); };
struct EVIIF { s16 delta,index; void (*fn)(char *,s32,s32,f32); };
struct Mat { char x[0x40]; };
struct View { char x[0x30]; };
struct Ctx { char x[0x250]; };
extern "C" {
void GSBuffer__getTexture(char *,View *); void func_0038E7F8(Ctx *,char *,char *,f32); void func_0038EFF8(Ctx *); s32 RaceEntryBase__sortEntryCar(char *,f32,f32,f32);
void ModelSet2__farClip(s32); void func_004A1638(s32); void pglFrontFace(s32); void func_004A53F8(); void func_004A5400(); void func_004A7550(Mat *);
void func_004A7774(Mat *); void func_004A7A54();void func_004A7B18(Mat *);void func_004AB040(s32);
}
extern "C" void RacePS2Base__render_reflection_cars(char *p,char *camera,f32 rate) {
    Mat m; View v; Ctx c; Mat m2;
    f32 b,a,zero;
    ModelSet2__farClip(1);
    zero=0.0f;
    pglFrontFace(1); func_004A1638(21);func_004A7550(&m);
    E *e=(E *)(F(p,char *,0x64)+0xD0);
    char *obj=((char *(*)(char *,s32))e->fn)(p+e->delta,0);
    char *r=F(p,char *,0x6C);
    char *list=F(r,char *,0x60);
    char *env=F(F(r,char *,0x80),char *,4);
    EF *ef=(EF *)(F(obj,char *,0)+0x30);
    a=ef->fn(obj+ef->delta,zero);
    EF *efb=(EF *)(F(obj,char *,0)+0x28);
    b=efb->fn(obj+efb->delta,zero);
    EF *efc=(EF *)(F(obj,char *,0)+0x20);
    RaceEntryBase__sortEntryCar(list,efc->fn(obj+efc->delta,zero),b,a);
    View *pv=&v;
    GSBuffer__getTexture(camera,pv);
    char *n=F(list,char *,0x10);
    if(n!=0) {
        f32 loopzero=zero;
        Ctx *pc=&c; Mat *pm=&m2;
        do {
            E *test=(E *)(F(obj,char *,0)+0x1A8);
            if(((s32 (*)(char *,char *,char *))test->fn)(obj+test->delta,F(p,char *,0x6C),n)!=0 && F(F(n,char *,0x18),unsigned char,0x545)==0) {
                E *test2=(E *)(F(p,char *,0x64)+0x4A0);
                if(((s32 (*)(char *,char *))test2->fn)(p+test2->delta,n)!=0) {
                    func_0038E7F8(pc,env,F(F(n,char *,0x2880),char *,0),loopzero);
                    func_004A53F8();
                    char *car=F(n,char *,0x2880);
                    EVF *vf=(EVF *)(F(car,char *,8)+0x118); vf->fn(car+vf->delta,loopzero);
                    func_004A53F8();func_004A7A54();func_004A7774(&m);func_004A7550(pm);func_004A7B18(pm);func_004A5400();
                    EVP *vp=(EVP *)(F(car,char *,8)+0xA8); vp->fn(car+vp->delta,(char *)pv);
                    func_0038EFF8(pc);
                    if(F(F(F(p,char *,0x6C),char *,0x60),s32,0)<3) {
                        EVIIF *vf2=(EVIIF *)(F(car,char *,8)+0xF0);vf2->fn(car+vf2->delta,1,1,rate);
                        EVIIF *vf2b=(EVIIF *)(F(car,char *,8)+0xF8);vf2b->fn(car+vf2b->delta,1,2,rate);
                    }
                    EVI *vi=(EVI *)(F(car,char *,8)+0x40);vi->fn(car+vi->delta,2);
                    EVIF *vf3=(EVIF *)(F(car,char *,8)+0xB8);vf3->fn(car+vf3->delta,1,rate);
                    func_004A5400();
                }
            }
            n=F(n,char *,4);
        }while(n!=0);
    }
    func_004AB040(21);pglFrontFace(0);ModelSet2__farClip(0);
}
