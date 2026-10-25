// Native correction of src/startup/unit_00101400/func_00103B40.cpp: the matched source reads the
// load result's data pointer (offset 0x10 of the 0x14-byte result) through a second local array
// that the PS2 compiler happens to place right after the first; natively it is a separate,
// uninitialised slot. Read it from one result block instead.
typedef int s32;
struct LoadResult {
    s32 w[4];
    s32 p;
};
extern "C" int func_004AE230(void *, s32, s32);
extern "C" void func_00498B28(s32);
extern "C" s32 func_00103B40(s32 arg0) {
    LoadResult r;
    func_004AE230(&r, arg0, 1);
    func_00498B28(r.p);
    return r.p;
}
