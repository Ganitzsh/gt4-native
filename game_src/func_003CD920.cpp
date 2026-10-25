/* compiler: ee-gcc2.96-no-strict-aliasing */
struct Tire { char p[0x38]; float f38; };
struct Hdr { char p[0xF8A0]; int count; };
struct Rec { char p[0x38C4]; int v38c4; char p1[0x3E08-0x38C8]; int v3e08; };
struct Self { char p[0x38C4]; };
extern "C" int func_003C9F18(char *self);
extern "C" float func_00344CE8(void *, int);
extern "C" void *func_0034C190(Hdr *, int);
extern "C" void func_003C9F00(char *, int) throw();
extern "C" Tire *func_003CAEE8(char *, int) throw();
extern "C" int Pitmen__getMotion(void *) throw();
extern "C" int func_003CC8E8(void *) throw();
extern "C" void func_00427820(void *);
extern "C" void GT4_Motion__MotionSetPlayer__set(void *, int, int);
extern "C" void func_00428D48(void *, int, int, int, int, int, float, float);

extern "C" void func_003CD920(char *self, Hdr *hdr)
{
    float a[4];
    int tmp[4];
    float b[4];
    int i, j;
    char *rec = self;
    for (i = 0; i < 4; i++) {
        char *t = rec + 0x38C8;
        func_003C9F00(t, i);
        func_00427820(tmp);
        int x = func_003CC8E8(self);
        GT4_Motion__MotionSetPlayer__set(tmp, x, Pitmen__getMotion(self));
        func_00428D48(tmp, *(int *)(rec + 0x38C4), (int)func_003C9F18, *(int *)(rec + 0x3E08), (int)(rec + 0x4354), 0, 0.0f, 0.0f);
        rec += 0xAA4;
        a[i] = func_003CAEE8(t, (int)"tire1")->f38;
    }
    int n = hdr->count;
    j = 0;
    if (j < n) {
    char *q = self + 8;
    do {
        float *out = (float *)(self + j * 16 + 0x91A0);
        i = 0;
        void *car = func_0034C190(hdr, j);
        int *in = (int *)(q + 0x9850);
        for (; i < 4; i++) {
            float v = 0x1.09374Ap-2f - func_00344CE8(car, *in++);
            b[i] = v;
            if (a[i] != 0.0f)
                *out++ = (v + a[i]) / a[i];
            else
                *out++ = 1.0f;
        }
        q += 16;
        j++;
    } while (j < n);
    }
}
/* native correction of src/race/unit_003CCAE0/func_003CD920.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_00428D48 */
