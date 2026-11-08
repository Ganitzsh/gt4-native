/* Native copy of 003403B8 (RacePS2Base::render_course, the main view's near course pass: CourseData::render_course or
 * 00397048, and render_course_for_renderer; not its extra model set, q->unk18 drawn when q->unkB4 is set: RaceLicense's
 * overlay the player toggles, lane B's review) from gt4-decomp's
 * src/race/RaceBGMPS2_RacePS2Base/RacePS2Base__render_course.cpp, the source the native game compiled until now, with its
 * Instances flagged kInstCourse (docs/modern/CONTRACTS.md, A11a). Nothing is sent while scene records are off. */
#include "gt4_gfx.h"
#include "spr.h"
struct Obj80 { char pad0[0xF30]; int unkF30; };
struct Obj4 { char pad0[8]; void *unk8; char pad1[0x80 - 0x0C]; Obj80 *unk80; };
struct Q80 { char pad0[4]; Obj4 *r4; char pad1[0x18 - 0x08]; int unk18; char pad2[0xB4 - 0x1C]; int unkB4; };
struct X6C { char pad0[0x80]; Q80 *q80; };
struct Self { char pad0[0x6C]; X6C *x6c; };

extern "C" int ModelSet2__texLOD(int arg0);
extern "C" void CourseData__render_course(void *arg0, void *arg1, void *arg2, int arg3);
extern "C" void func_00397048(void *arg0, void *arg1);
extern "C" void CourseData__render_course_for_renderer(float, void *);
extern "C" void ModelSet2__render(int arg0, int arg1);

extern "C" void RacePS2Base__render_course(Self *arg0, void *arg1, int arg2) {
    gt4_scene_course(spr_base(), 1);   /* the course's draws (kInstCourse, A11a) */
    Obj4 *s2 = arg0->x6c->q80->r4;
    ModelSet2__texLOD(0);
    if (arg2 != 0) {
        CourseData__render_course(s2, s2->unk8, arg1, s2->unk80->unkF30);
    } else {
        func_00397048(s2, arg1);
    }
    CourseData__render_course_for_renderer(0.0f, s2);
    gt4_scene_course(spr_base(), 0);   /* (before the overlay below: a licence test's guide, toggled by the player, not the course) */
    Q80 *q = arg0->x6c->q80;
    int v = q->unk18;
    if (v != 0 && q->unkB4 != 0) {
        ModelSet2__render(v, 0);
    }
}
