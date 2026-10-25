/* compiler: ee-gcc2.96-no-strict-aliasing */
struct O {
    char pad[0x30];
    int i30; int i34;
    int a38[6]; int a50[6]; int a68[6];
    float f80[6]; float f98[6];
    int aB0[6]; int aC8[6];
};
struct Ctx3 { char pad[0xF8A0]; int n; };
struct Ctx2 { char pad[0x9684]; int tbl[1]; };
struct Msg { float v[4]; int flag; int val; };
extern "C" void func_003CBA88(O *, int, void *, int) throw();
extern "C" int func_003CBD00(O *, Ctx3 *, int, void *, int, int, int *);
extern "C" int func_00427800(float *, int, float, float);
extern "C" int func_00429BC0(void *, void *);
extern "C" void func_003CBB28(O *o, void *a1, Ctx2 *a2, Ctx3 *a3, float f)
{
    int i = 0;
    int sp0;
    Msg m;
    o->i30 = 0;
    o->i34 = 0;
    if (a3->n > 0) {
        do {
            int tb = a2->tbl[i];
            o->a38[i] = 0;
            sp0 = 0;
            int r = func_003CBD00(o, a3, i, a2, o->aB0[i], tb, &sp0);
            o->aB0[i] = r;
            if (r < 0) {
                o->f98[i] = o->f80[i] = 0.0f;
                o->a50[i] = 0;
                o->a68[i] = 0;
            } else {
                o->f98[i] = o->f98[i] + f;
                if (sp0 != 0) {
                    func_003CBA88(o, i, a2, o->aB0[i]);
                } else {
                    float *pv = m.v;
                    func_00427800(pv, o->aC8[i], o->f80[i], f);
                    m.flag = 1;
                    m.val = o->aB0[i];
                    func_00429BC0(a1, &m);
                    o->f80[i] = *pv;
                }
            }
            i++;
        } while (i < a3->n);
    }
}
/* native correction of src/race/unit_003C8BF8/func_003CBB28.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_00427800 */
