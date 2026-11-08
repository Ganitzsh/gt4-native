/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native copy of RacePS2Base::render_cars (00340950, from its draft): the cars of a view. The car model's
 * renderLayer (+0xD0, 00390728: flag, time) and setup (+0xF0, 00390F70: two ints, time) take their ints first; the
 * draft declared the float first, the same call on the EE (floats in their own registers) but not natively: each
 * extra layer of a car (rc.count >= 2) got the time's bits as its flag and ~0 as its time (drawn where the car
 * was, or untransformed when the time was 0), and setup's driver and crew the same.
 * A6: each car's light-map pass (0038F5D0: the course lamps near the car as fans in its reflection texture) runs in a
 * light-map context with the car's entrant (car +0x544), so its batch runs are kInstLightMap Instances
 * (runtime/native/pgl.cpp; CONTRACTS.md a.5.7). */
#include "gt4_gfx.h"
#include "spr.h"
#define I(p,o) (*(int *)((char *)(p)+(o)))
#define U(p,o) (*(unsigned *)((char *)(p)+(o)))
#define F(p,o) (*(float *)((char *)(p)+(o)))
#define P(p,o) (*(char **)((char *)(p)+(o)))
struct Vec3 { float x,y,z; };
class Node { public: virtual void m0(); virtual void m1(); virtual void m2(); virtual void m3(); virtual Vec3 position(float); };
class Item;
struct Car { char pad[0x2880]; Item *item; };
class View;
class Race {
char pad[0x64];
public:
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
virtual void m23();
virtual void m24();
virtual View *view(int);
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
virtual void renderOne(int,Item *);
virtual void m115();
virtual void m116();
virtual void m117();
virtual void m118();
virtual void m119();
virtual void m120();
virtual void m121();
virtual void m122();
virtual void m123();
virtual void m124();
virtual void m125();
virtual void m126();
virtual void m127();
virtual void m128();
virtual void m129();
virtual void m130();
virtual void m131();
virtual void m132();
virtual void m133();
virtual void m134();
virtual void m135();
virtual void m136();
virtual void m137();
virtual void m138();
virtual void m139();
virtual void m140();
virtual void m141();
virtual void m142();
virtual void m143();
virtual void prepare();
virtual int isRender(Car *, View *,float);
};
class View {
public:
virtual void m0();
virtual void m1();
virtual void m2();
virtual void m3();
virtual void m4();
virtual void m5();
virtual void m6();
virtual void m7();
virtual void direction(Vec3 *,float);
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
virtual Car *car();
};
class Item { public: Node *node; char *car;
virtual void m0();
virtual void m1();
virtual void m2();
virtual void m3();
virtual void m4();
virtual void m5();
virtual void m6();
virtual void mode(int);
virtual void alpha(float);
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
virtual int texture(void *);
virtual void render(int,bool,float);
virtual void m22();
virtual void m23();
virtual void renderBase(bool,int,int,float);
virtual void renderLayer(int,float);
virtual void m26();
virtual void light(int,int,float);
virtual void m28();
virtual void setup(int,int,float);
virtual void effect(float,int,int);
virtual void finish();
};
struct List { unsigned count; int pad; Car **cars; };
struct Entry { Item *item; float distance; };
struct Sorter { Entry items[6]; int count; Vec3 *direction; float time; Entry *begin(){return items;} Entry *end(){return items+count;} };
struct Iterator {
    Entry *p;
    Item *&operator*() const { return p->item; }
    Iterator &operator++() { ++p; return *this; }
    bool operator!=(Entry *q) const { return p!=q; }
};
static inline Iterator IteratorEnd(Entry *p) { Iterator i = {p}; return i; }
struct RenderContext { char pad[0x230]; int count; int pad234; int enabled; char tail[0x14]; };
struct Screen { char pad[0x28]; int mode; char tail[0x10]; };
struct Screens { Screen s[2]; int index; Screen *current() { return &s[index]; } };
extern Screens D_0061850C;
extern char D_006883F8[];
extern "C" {
void func_00105250(void *);
void func_00105280(void *,int);
void *func_00105378(void *,void *);
void func_00105460(void *,int,int);
int func_00105528(void *);
void func_00105930(void *,void *);
int func_00340550(Item *,float) throw();
void func_00340738(Sorter *,Item *) throw();
void func_003407D8(Sorter *) throw();
int func_00366678(void *);
void func_0038E7F8(RenderContext *,void *,Node *,float);
void func_0038EFF8(RenderContext *);
void func_0038F168(RenderContext *,int);
void func_0038F240(RenderContext *);
void func_0038F5D0(RenderContext *,void *,void *,void *,bool);
char *func_00396800(void *);
void func_00397E38(void *);
void func_00398718(void *);
int func_003D2A68(void *,void *);
int func_003ED928(Item *,Item *,float);
void func_0044EEA8(void *);
void func_0044EF18(void *,float,float,float);
void func_0044F198(void *,int);
void func_004567B0(int);
void func_004568C0(void *);
void func_004A5FE0(int);
}
struct GSBuffer { char pad[0x40]; GSBuffer() { func_00105250(this); } ~GSBuffer(){func_00105280(this,2);} GSBuffer &operator=(GSBuffer &s) {func_00105378(this,&s);return *this;} void size(int w,int h) {func_00105460(this,w,h);} void apply(){func_00105528(this);} void texture(void *p){func_00105930(this,p);} };
struct Camera { char pad[0x70]; Camera(){func_0044EEA8(this);} ~Camera(){*(void **)this=D_006883F8;func_0044F198(this,0);} void position(const Vec3 &p) {func_0044EF18(this,p.x,p.y,p.z);} };
static inline bool isLarge(char *rw) { return *(unsigned short *)(rw+0x40)!=0; }
static inline int readInt(int *p) { return *p; }
extern "C" void func_00340950(char *self, GSBuffer *buffer, int mode, int effects, int carIndex, float time) {
    func_004567B0(1);
    char *data=P(P(P(P(self,0x6c),0x80),4),0x80);
    func_00397E38(data); func_00398718(data);
    char *course=P(P(P(self,0x6c),0x80),4);
    View *view=((Race *)self)->view(0);
    List *list=(List *)P(P(self,0x6c),0x60);
    Vec3 direction;
    view->direction(&direction,0.0f);
    Sorter sort;
    sort.count=0; sort.direction=&direction; sort.time=time;
    for(unsigned i=0;i<list->count;++i) {
        if(((Race *)self)->isRender(list->cars[i],view,time)) func_00340738(&sort,list->cars[i]->item);
    }
    func_003407D8(&sort);
    Item *first=list->cars[0]->item;
    Item *focus=view->car()->item;
    ((Race *)self)->prepare();
    char texture[0x30];
    GSBuffer target;
    target=*buffer;
    if(isLarge(func_00396800(course))) {target.size(256,256);target.apply();}
    target.texture(texture);
    Sorter *ps = &sort;
    Iterator it = {ps->begin()};
    for(;it!=ps->end();++it) {
        char *car=(*it)->car;
        if(carIndex>=0 && (unsigned char)car[0x544]!=carIndex) continue;
        if(!func_00340550(*it,time)) continue;
        if(func_00366678(car)==2) {
            if(func_003ED928(*it,first,0.0f)) continue;
            if(D_0061850C.current()->mode==0) func_004A5FE0((I(self,0xe32c)&1)?3:4);
            else func_004A5FE0(1);
        }
        (*it)->mode(mode);
        if(*it==focus) focus->alpha(F(data,0x54)*0.5f);
        else (*it)->alpha(F(data,0x54));
        RenderContext rc;
        func_0038E7F8(&rc,course,(*it)->node,time);
        if(rc.enabled) {
            Vec3 pos=(*it)->node->position(time);
            Camera camera;
            camera.position(pos);
            gt4_scene_light_map(spr_base(),1,(unsigned char)car[0x544]);
            func_0038F5D0(&rc,&target,buffer,&camera,*it==focus);
            gt4_scene_light_map(spr_base(),0,0);
        }
        bool simple=false;
        func_0038EFF8(&rc);
        (*it)->setup(mode>=2,1,time);
        if(rc.count>=2) {
            int n=rc.count;
            if(*it!=focus) n=n>=3?2:n;
            for(int j=0;j<n;++j) {func_0038F168(&rc,j);(*it)->renderLayer(1,time);}
        }
        else simple=true;
        func_0038F240(&rc);
        bool show=true;
        if(carIndex>=0) show=func_003D2A68(self+0x3628,car)!=0;
        (*it)->renderBase(simple,1,1,time);
        if(!I(self,0xe348)) {
            (*it)->light(1,readInt((int *)(self+0x3624)),time);
            int flag=*it==focus?16:1;
            (*it)->texture(texture);
            (*it)->effect(time,1,mode);
            (*it)->render(effects?flag:0,show,time);
            (*it)->finish();
            (*it)->light(0,readInt((int *)(self+0x3624)),time);
        }
        if(carIndex>=0) ((Race *)self)->renderOne(carIndex,*it);
        func_004A5FE0(0);
    }
    func_004568C0(0); func_004567B0(0);
}
