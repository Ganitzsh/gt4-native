/* compiler: ee-gcc2.96-no-strict-aliasing */
struct Gl { char p[0x84]; struct Gl2 *m84; };
struct Gl2 { char p[0xD54]; int mD54; };
struct Self {
    Gl *m0;
    char pad[0xCBE5 - 4];
    signed char mCBE5;
    char pad2[0xF898 - 0xCBE6];
    unsigned mF898;
    char pad3[0x10140 - 0xF89C];
    virtual int v0();
    virtual int v1();
    virtual int v2();
    virtual int v3();
    virtual int v4();
    virtual int v5();
    virtual int v6();
    virtual int v7();
    virtual int v8();
    virtual int v9();
    virtual int v10();
    virtual int v11();
    virtual int v12();
    virtual int v13();
    virtual int v14();
    virtual int v15();
    virtual void v16(unsigned char a, int b);
};
struct Pt { char p[2]; unsigned char m2; signed char m3; char q[8]; float mC; };
struct E7 { char p[0x440]; unsigned char m440; char q[0x4A4 - 0x441]; float m4A4; char r[0x614 - 0x4A8]; unsigned m614; };
struct E { char p[0xC]; struct E6 *m6; char q[0xF4]; E7 e7; };
struct E6 { char p[0xD4]; char m[0xFC]; int m1D0; };
extern "C" E *func_0034C190(Self *, int);
extern "C" Pt *func_00346A28(void *);
extern "C" int func_0033CF48(void);
extern "C" float func_00579398(float);
extern "C" void func_003F58B0(void *, void *, int, float);
extern "C" void Automobile__isDataLoggerActive(void *, int, int, float, float, float) throw();

extern "C" void DynamicsConductor__playbackOneLapReplay(Self *self, int idx) {
    E *e = func_0034C190(self, idx);
    E6 *s6 = e->m6;
    E7 *s7 = &e->e7;
    if (s6->m1D0 == 0) {
        int fp = 1;
        Pt *pt = func_00346A28(s6->m);
        if (func_0033CF48() != 0) {
            fp = 0;
            self->mCBE5 = (signed char)(int)func_00579398(0x1.7eb85p+1f);
            self->m0->m84->mD54 = 1;
        }
        unsigned div = 3;
        float f = pt->mC;
        unsigned char m2 = pt->m2;
        unsigned pos = 100;
        pos -= m2;
        self->mF898 = 100;
        unsigned q = pos / div;
        int v = pt->m3 - 1;
        pos += v;
        self->v16(s7->m440, v);
        q = pos / 3;
        func_003F58B0(s6, e, fp, f);
        s7->m614 = q;
        return Automobile__isDataLoggerActive(e, 0, m2, f, 0.0f, s7->m4A4);
    }
}
/* native correction of src/race/unit_0034E9A0/DynamicsConductor__playbackOneLapReplay.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes Automobile__isDataLoggerActive */
