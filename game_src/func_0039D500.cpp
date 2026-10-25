/* compiler: ee-gcc2.96-no-strict-aliasing */
struct Self { char pad0[0x20]; unsigned int mode; char pad24[0xBC - 0x24]; float alpha; };
struct Msg { char *s0; char *s4; char pad8[0x14C - 8]; char *s14C; };
struct Obj { int a, mode; };
extern Obj D_00623838;
extern char D_006A0470[];
extern Msg *DisplayRText__rtext_ptrs_;
extern "C" {
float func_003AFD18(int, float, float);
float RaceDisplayUtil__wideOffset(int);
unsigned func_003AFEC8(unsigned, float);
void func_0044D920(Obj *, char *);
void func_0044D988(Obj *);
float func_0044DC30(Obj *);
void func_0044DDF8(Obj *, float, float, float);
void func_0044DE80(Obj *, unsigned);
float func_0044DFC8(Obj *, char *);
void func_0044E030(Obj *, char *);
void func_0057DA20(char *, char *, ...);
}

extern "C" void func_0039D500(Self *self, int count) {
    char buf[32];
    if (self->mode < 2) {
        float *alpha = &self->alpha;
        count -= 3;
        if (count < 0) count = 0;
        func_0044DE80(&D_00623838, func_003AFEC8(count >= 10 ? 0x80C8C8C8 : 0x800000FF, *alpha));
        func_0044D920(&D_00623838, D_006A0470);
        float x = RaceDisplayUtil__wideOffset(4) + 24.0f;
        func_0044DDF8(&D_00623838, x, 335.0f, 0.0f);
        func_0044E030(&D_00623838, DisplayRText__rtext_ptrs_->s0);
        func_0044DDF8(&D_00623838, x, 351.0f, 0.0f);
        func_0057DA20(buf, DisplayRText__rtext_ptrs_->s14C, count);
        func_0044E030(&D_00623838, buf);
        if (count <= 0) {
            func_0044D920(&D_00623838, D_006A0470);
            func_0044DE80(&D_00623838, func_003AFEC8(0x800000FF, *alpha));
            char *text = DisplayRText__rtext_ptrs_->s4;
            float scale = func_0044DC30(&D_00623838);
            func_0044DDF8(&D_00623838, func_003AFD18(2, 320.0f, func_0044DFC8(&D_00623838, text) / scale), 224.0f, 0.0f);
            func_0044E030(&D_00623838, text);
        }
        func_0044D988(&D_00623838);
    }
}
/* native correction of src/race/RaceDisplay/func_0039D500.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_003AFD18 */
