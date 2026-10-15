// Imported unchanged from gustavocanepa/gt4-decomp
// Upstream commit: de3d7edbead9e8a6e473b1e68b8ddf3b1f639fd1
// Upstream path: src/runtime/MboolReader/MboolReader__structor_19.cpp
// Attribution and licensing: see THIRD_PARTY_NOTICES.md
extern "C" void MReaderBase__structor_0(void *arg0, void *arg1);
extern "C" void func_005C1628(void *arg0);

extern void *MboolReader__vtable;

extern "C" void MboolReader__structor_19(void *arg0, int arg1) {
    *(void **)arg0 = &MboolReader__vtable;
    MReaderBase__structor_0(arg0, 0);
    if (arg1 & 1) {
        return func_005C1628(arg0);
    }
}
