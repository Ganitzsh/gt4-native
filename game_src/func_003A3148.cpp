struct VEntry {
    short delta;
    short index;
    int (*fn)(void *);
};
struct VObj {
    int m0;
    VEntry *vtbl;
};
static inline int vcall(VObj *o, int slot)
{
    VEntry *e = &o->vtbl[slot];
    return e->fn((char *)o + e->delta);
}
struct Target { char pad[0xF4]; char mF4[4]; };
extern "C" int func_003B76F0(void *p, int a, int b, int n);

extern "C" int RaceDisplayEventBase__put(VObj *o, Target *t)
{
    int a = vcall(o, 1);
    int b = vcall(o, 2);
    return func_003B76F0(t->mF4, a, b, 2);
}
/* native correction of src/race/unit_003A3130/RaceDisplayEventBase__put.cpp: returns the int of its last call func_003B76F0 (left in $v0). */
