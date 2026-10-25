extern "C" void *SparkParticle__structor_0(void *owner);
extern "C" void func_003C1358(void *, int, int, int, int, float, float, float);

extern "C" void func_003C20C8(void *owner, int a, int b, float x, float y, int c, float z, int d) {
    void *p = SparkParticle__structor_0(owner);
    if (p)
        func_003C1358(p, a, b, c, d, x, y, z);
}
/* native correction of src/race/ParticleManager_SparkParticle/func_003C20C8.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_003C1358 */
