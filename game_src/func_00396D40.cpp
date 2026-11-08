/* Native copy of 00396D40 (the course's far pass) from gt4-decomp's src/race/unit_00394D48/func_00396D40.cpp, the source the
 * native game compiled until now, with its Instances flagged kInstCourse (docs/modern/CONTRACTS.md, A11a). Nothing is sent
 * while scene records are off. */
#include "gt4_gfx.h"
#include "spr.h"
/* Draws the model set held at panel+0x58, if any, in a separate render pass (the pgl calls are the Sony graphics
 * library, inferred from the pglPushAttrib name). It pushes attributes (0x40), sets colours through func_004A2980
 * (0xFF000000, then -1) and mode values through func_004A3078 / func_004A3230, updates a sub-object at (pointer at
 * +0x80) + 0x60 with 1.0 through func_00397DA0, reads a 16-float matrix with func_004A7550, multiplies element 14
 * by 100 (element 14 of a 4x4 matrix is the z translation in column-major order, inferred) and writes the matrix
 * back with func_004A74B4 before the model set is rendered with ModelSet2__render. The calls to
 * RaceEntryCar__update with 1 and 0 around it are symbol-table names for helpers whose real purpose is unknown,
 * and so are func_004A53F8 / func_004A5400 / func_004A5688, which look like a matching begin and end set (inferred
 * from their order). */
#include "types.h"

extern "C" void func_00397DA0(void *self, f32 f);
extern "C" void ModelSet2__render(s32 a0, s32 a1);
extern "C" void func_004A2980(s32 a0);
extern "C" void func_004A3078(s32 a0);
extern "C" void func_004A3230(s32 a0);
extern "C" void RaceEntryCar__update(s32 a0);
extern "C" void func_004A53F8(void);
extern "C" void func_004A5400(void);
extern "C" void pglPushAttrib(s32 a0);
extern "C" void func_004A5688(void);
extern "C" void func_004A74B4(f32 *a0);
extern "C" void func_004A7550(f32 *a0);

extern "C" void func_00396D40(char *panel) {
    gt4_scene_course(spr_base(), 1);   /* the course's draws (kInstCourse, A11a) */
    f32 matrix[16];
    if (*(s32 *)(panel + 0x58) != 0) {
        pglPushAttrib(0x40);
        func_004A2980((s32)0xFF000000U);
        func_004A3078(0);
        func_004A3230(3);
        func_004A2980(-1);
        func_00397DA0(*(char **)(panel + 0x80) + 0x60, 1.0f);
        RaceEntryCar__update(1);
        func_004A53F8();
        func_004A7550(matrix);
        matrix[14] = matrix[14] * 100.0f;
        func_004A74B4(matrix);
        RaceEntryCar__update(0);
        ModelSet2__render(*(s32 *)(panel + 0x58), 0);
        RaceEntryCar__update(1);
        func_004A5400();
        RaceEntryCar__update(0);
        func_004A5688();
    }
    gt4_scene_course(spr_base(), 0);
}
