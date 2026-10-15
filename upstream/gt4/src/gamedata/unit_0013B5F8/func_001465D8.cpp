// Imported from gustavocanepa/gt4-decomp @ de3d7edbead9e8a6e473b1e68b8ddf3b1f639fd1
// Original path: src/gamedata/unit_0013B5F8/func_001465D8.cpp
typedef int s32;
typedef float f32;

struct Obj001465D8 {
    char pad0[0x178];
    s32 unk178;
};

extern "C" f32 func_001465D8(struct Obj001465D8 *arg0) {
    return (f32)arg0->unk178 / 100.0f;
}
