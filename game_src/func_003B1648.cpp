/* compiler: ee-gcc2.96-no-strict-aliasing */
struct G {
    char pad[0x28];
    float f28;
};

extern G D_00845380;
extern "C" float func_00455000(float, void *);
extern "C" void func_004A53F8(void);
extern "C" void func_004A5400(void);
extern "C" void func_004A7550(void *);
extern "C" void func_004A7844(float, float, float);
extern "C" void func_004A7988(float);
extern "C" void func_004A79B0(float);
extern "C" void func_004A79D8(float);
extern "C" void *func_005A48D8(void *, int, unsigned int);

extern "C" void func_003B1648(int unused, float *o1, float *o2, float *pos, float *rot, float *tgt) {
    struct { float m[16]; float v[3]; } L;
    func_004A53F8();
    func_004A7844(pos[0], pos[1], pos[2]);
    func_004A79B0(rot[1] * 57.29578f);
    func_004A7988(rot[0] * 57.29578f);
    func_004A79D8(rot[2] * 57.29578f);
    func_004A7844(tgt[0], tgt[1], tgt[2]);
    func_005A48D8(L.v, 0, 0xC);
    float d = func_00455000(1.0f, L.v);
    func_004A7550(&D_00845380);
    *o2 = D_00845380.f28;
    func_004A5400();
    *o1 = d / 420.0f;
}
/* native correction of src/race/unit_003B1070/func_003B1648.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_00455000 */
