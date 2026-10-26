/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native correction of src/race/unit_004271E8/func_0042A660.cpp (matched): its two virtual calls
 * (slots 0x138 and 0x130) pass the object only, and the EE leaves this function's own arg1 (a model
 * name) in $a1 for them; a target that reads it (0042A9C8) got stack garbage natively. Passed explicitly. */
struct VE { short adj; short pad; void *(*fn)(void *, int); };
struct VT { char pad[0x130]; VE e130; VE e138; };
struct Ent { int p0; int p4; };
struct Mtx { char pad[0x30]; float x, y, z; char pad2[4]; int idx; };
struct Obj {
    int p0; VT *vt; int p8; int pc; int p10; int p14; int p18;
    float x, y, z;
};
extern "C" {
int func_0042A658(Obj *) throw();
int func_0042A828(Obj *, unsigned short);
Ent *ModelSet2__searchModelName(int, int, int *);
Mtx *func_00454970(int, int);
void ModelSet2___render(int, int, int);
void func_004A53F8();
void func_004A5400();
void func_004A7550(void *);
void func_004A7698(void *);

void func_0042A660(Obj *o, int arg1) {
    int n;
    VE *e = &o->vt->e138;
    void *r = e->fn((char *)o + e->adj, arg1);
    if (r) {
        func_004A7550(r);
    } else if (func_0042A658(o)) {
        int i = 0;
        Ent *p = ModelSet2__searchModelName(func_0042A658(o), arg1, &n);
        if (n > 0) {
            do {
                if (func_0042A828(o, *(unsigned short *)&p->p4))
                    ModelSet2___render(func_0042A658(o), p->p4, o->p10);
                i++;
                p++;
            } while (i < n);
        }
    }
    VE *e2 = &o->vt->e130;
    char *base = (char *)e2->fn((char *)o + e2->adj, arg1);
    if (base && func_0042A658(o)) {
        Mtx *m = func_00454970(func_0042A658(o), arg1);
        if (m) {
            func_004A53F8();
            Mtx *s = (Mtx *)(base + (m->idx << 6));
            func_004A7698(m);
            func_004A7550(s);
            if (o->p18 != 1) {
                o->x = s->x;
                o->y = s->y;
                o->z = s->z;
            }
            s->x = s->x - o->x;
            s->y = s->y - o->y;
            s->z = s->z - o->z;
            func_004A5400();
            o->p18 = 1;
        }
    }
}
}
