struct S{char pad[8];int*p;char pad2[8];void*q;};extern "C" int func_003863A8(int*),func_003863B8(int*),func_003863C8(int*);extern "C" void func_0040DC08(void *, float *, float *, float *, float, float);
extern "C" void func_0040DD38(S*s,float a,float b){int x=func_003863A8(s->p);int y=func_003863B8(s->p);int z=func_003863C8(s->p);func_0040DC08(s->q, (float*)x, (float*)y, (float*)z, a, b);}
/* native correction of src/race/unit_00408DB8/func_0040DD38.cpp (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_0040DC08 */
