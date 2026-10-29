/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Randomly (re)configures the target object stored in slot idx of self (stride 0x19C, current at +0x158, new at
 * +0x13C). The kind is chosen with the float rng: below 0.3 -> 0, below 0.6 -> 5, else a random pick from the
 * candidate list func_003FF688 returns for the context (inferred from the thresholds). The target is then
 * initialised through virtual calls (vtable +0x140, +0x170, +0x150), may get a random value in [60,80) at +0x188, and
 * a few random bit fields in the flag words +0x260/+0x268. Game meaning of the fields is purpose unknown.
 * The virtual calls use the {delta,index,fn} entry form that gcc 2.96 needs for the thunk adjustment. */
typedef unsigned long u64;
struct Obj { char *vt; };
struct Tgt { char *vt; char p4[0x184]; float f188; int i18C; char p190[0xD0]; u64 f260, f268; };
struct GetEnt { short delta,index; Tgt *(*fn)(void *, int, float); };
struct IntEnt { short delta,index; int (*fn)(void *); };
struct VoidEnt { short delta,index; void (*fn)(void *); };
struct InitEnt { short delta,index; void (*fn)(void *, char *, int); };
struct FloatEnt { short delta,index; void (*fn)(void *, int, float); };
extern "C" int func_003FF688(int, int *, int);
extern "C" float func_00578598(void *);
extern "C" void func_0037BC48(Tgt *, int, int);
extern "C" void func_00379FA8(Tgt *, int, float);
static inline Tgt *get(Obj *self, int idx, float time) {
    GetEnt *e=(GetEnt *)(self->vt+0xD8);
    return e->fn((char *)self+e->delta,idx,time);
}
static inline int kind(Tgt *t) {
    IntEnt *e=(IntEnt *)(t->vt+0xB8);
    return e->fn((char *)t+e->delta);
}
static inline float affine(float x, float a, float b) { return x*a+b; }
extern "C" int func_00376238(Obj *self, int slotIndex, int a2, char *context, void *rng, float time) {
    int candidates[26];
    int previousKind = -1;
    Tgt *target=*(Tgt **)((char *)self+slotIndex*0x19C+0x158);
    if (get(self,slotIndex,time)==target) {
        if (target->i18C==*(int *)(context+0x10)) previousKind=kind(target);
    }
    int candidateCount=func_003FF688(*(int *)(context+0x10),candidates,26);
    float roll=func_00578598(rng);
    int choice=0;
    if (!(roll<0x1.333332p-2f)) {
        choice=5;
        if (!(roll<0x1.333332p-1f) && candidateCount>0) {
            int pick=(int)(func_00578598(rng)*(float)candidateCount);
            int lastIndex=candidateCount-1;
            if (pick<=0) pick=0;
            if (pick>=lastIndex) pick=lastIndex;
            choice=candidates[pick];
        }
    }
    InitEnt *initEntry=(InitEnt *)(target->vt+0x140);
    initEntry->fn((char *)target+initEntry->delta,context,choice);
    VoidEnt *resetEntry=(VoidEnt *)(target->vt+0x170);
    resetEntry->fn((char *)target+resetEntry->delta);
    target->f268=(target->f268 & ~0xFFul)|1;
    FloatEnt *timeEntry=(FloatEnt *)(target->vt+0x150);
    timeEntry->fn((char *)target+timeEntry->delta,3,time);
    if (kind(target)==previousKind) func_0037BC48(target,previousKind+1,1);
    int kindNow=kind(target);
    if (kindNow==0 || kindNow==5) {
        roll=func_00578598(rng);
        roll=affine(roll,20.0f,60.0f);
        target->f260=(target->f260 & 0x00FFFFFFFFFFFFFFul)|0x100000000000000ul;
        target->f188=roll;
        if (func_00578598(rng)<0.5f) target->f268=(target->f268 & ~0xFF00ul)|0x100ul;
    }
    if (func_00578598(rng)<0x1.999998p-2f) {
        int extraFlag=1;
        switch (kind(target)) {
        case 0: case 5: case 14 ... 17: extraFlag=0; break;
        default: break;
        }
        func_00379FA8(target, extraFlag, time);
    }
    if (func_00578598(rng)<0x1.333332p-2f) {
        int kindNow=kind(target);
        if (kindNow==0 || kindNow==5) target->f260=(target->f260 & 0xFFFF00FFFFFFFFFFul)|0x10000000000ul;
    }
    *(Tgt **)((char *)self+slotIndex*0x19C+0x13C)=target;
    return 1;
}
/* native correction of src/race/unit_00374E90/func_00376238.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_00379FA8 */
