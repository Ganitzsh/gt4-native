struct Glob { char p[0xE4]; int mE4; };
struct Ent { char p[0x5A8]; float f5A8; };
struct Self {
    Glob *m0;
    char pad[0xCBE4 - 4];
    signed char mCBE4;
    char pad2[0xF898 - 0xCBE5];
    unsigned mF898;
    unsigned mF89C;
    int mF8A0;
    char pad3[0x10140 - 0xF8A4];
    virtual int v0();
    virtual int v1();
    virtual int v2();
    virtual int v3();
    virtual int v4();
    virtual int v5();
    virtual int v6();
    virtual int v7();
};
extern "C" Ent *func_0034C190(Self *, int);
extern "C" int DynamicsConductor__GetCountDown(Self *, int);
extern "C" int func_0034C210(Self *);
extern "C" int func_00359AF0(Ent *);
extern "C" void Automobile__isDataLoggerActive(Ent *, int, int, float, float, float);

extern "C" int DynamicsConductor__ProcessLapTimeCommonPost(Self *self) {
    if (DynamicsConductor__GetCountDown(self, -1) == 1) {
        if (self->v6() != 0 && self->m0->mE4 != 0) {
            for (int i = 0; i < self->mF8A0; i++) {
                Ent *e = func_0034C190(self, i);
                if (func_00359AF0(e) != 0)
                    Automobile__isDataLoggerActive(e, 0, 0, 1.0f, 0.0f, e->f5A8);
            }
        }
    }
    int r = func_0034C210(self);
    if (r == 0) {
        self->mF89C += 0x32;
        if (self->mF89C > 0x405F7DFD) self->mF89C = 0x405F7DFD;
        if (self->mCBE4 == 0) {
            unsigned t = self->mF898 + 0x32;
            self->mF898 = t;
            if (self->mF898 > 0x405F7DFD) self->mF898 = 0x405F7DFD;
            return (int)t;
        }
        return self->mCBE4;
    }
    return r;
}
/* native correction of src/race/unit_0034E9A0/DynamicsConductor__ProcessLapTimeCommonPost.cpp: returns what $v0 holds at each PS2 return: func_0034C210's nonzero result, else mCBE4 when nonzero, else mF898 + 0x32 before the clamp. */
