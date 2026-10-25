/* compiler: ee-gcc2.96-no-strict-aliasing */
struct Ent { int f0; const char *name; int pad[4]; };
struct Tab { Ent *base; int pad[3]; };
extern int D_00623A2C;
extern int D_00623A28;
extern char D_00846918[];
extern "C" int func_00461C00(int) throw() __attribute__((const));
extern "C" void func_00461C28(int, int, int, float) throw();
extern "C" Tab *func_00461E50(Tab *, int) throw();
extern "C" void func_00576788(char *, int a1, int a2);
extern "C" void func_005767C0(char *);
extern "C" int func_0057F238(const char *, const char *) throw();
extern "C" void func_00462020(int a0, int a1, int a2, float f)
{
    int need = 0;
    int k, cur;
    func_00576788(D_00846918, a1, a2);
    if (a1 != D_00623A2C) {
        need = 1;
    } else if (a1 != -1) {
        if (a1 & 0x1000) {
            need = 1;
        } else {
            k = func_00461C00(a0);
            cur = D_00623A28;
            if (k != func_00461C00(cur)) {
            set:
                need = 1;
            } else {
                Tab x;
                Tab y;
                func_00461E50(&x, cur);
                func_00461E50(&y, a0);
                switch (k) {
                case 1:
                    {
                        Ent *e0 = x.base;
                        e0 += D_00623A2C;
                        Ent *e1 = y.base;
                        e1 += a1;
                        need = func_0057F238(e0->name, e1->name) != 0;
                    }
                    break;
                case 0:
                    need = (D_00623A28 ^ a0) != 0;
                    break;
                }
            }
        }
    } else {
        goto set;
    }
    if (need) func_00461C28(a0, a1, a2, f);
    func_005767C0(D_00846918);
}
/* native correction of src/engine/unit_00457510/func_00462020.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_00461C28 */
